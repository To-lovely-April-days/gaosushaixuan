// ============================================================================
// 文件名：Eject.cpp
// 作用  ：阀控通信模块的实现（串口版本）
//
// 通信流程：
//   1) 程序启动：调 Start_send() 打开 COM 口
//   2) 每批数据：调 ControlValvesWithRows()
//        a) 根据 sel_type[] 过滤要剔除的种类
//        b) 5x5 区域和过滤（积分图加速）
//        c) 十字膨胀（可选）
//        d) 50 帧取并集 → 192 阀触发状态
//        e) 合并连续阀号 → 每段一条 7 字节命令
//        f) 串口发送
//   3) 程序退出：调 Stop_send() 关闭串口
//
// 7 字节协议详解：
//   字节 0: 0xFF                    （帧头）
//   字节 1: start_idx (1 字节)      （起始阀号，1~192）
//   字节 2: end_idx   (1 字节)      （结束阀号，1~192）
//   字节 3: delay_h   (1 字节)      （延时高字节）
//   字节 4: delay_l   (1 字节)      （延时低字节）
//   字节 5: duration_h(1 字节)      （吹气时长高字节）
//   字节 6: duration_l(1 字节)      （吹气时长低字节）
// ============================================================================
#include "Eject.h"
// ============================================================================
// 关键：必须先 include WinSock2.h 再 include windows.h
// 否则 windows.h 会先拉入旧的 winsock.h，与 WinSock2.h 冲突，
// 报 "WSADATA / SOCKET / hostent 已定义" 等大量错误
// 也可以用 #define WIN32_LEAN_AND_MEAN 让 windows.h 不拉入 winsock.h
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

// ============================================================================
// 外部全局变量（在 main.cpp 中定义）
// ============================================================================
extern int dealCount;   // 一批处理的帧数（默认 50）
extern int g_Samples;   // 每帧像素数（默认 640）
extern int frameq;      // 帧率

// ============================================================================
// 心跳字节
// ============================================================================
char firstByte = 0;

// ============================================================================
// 操作屏参数（由 UDP_receive_thread 更新；也有合理默认值）
// ============================================================================
int sendok = 0;
int start_line = 15;       // 兼容字段（已被 SetPixelOffset 取代）
int end_line = 593;       // 兼容字段
int sel_type[12] = { 0 };  // 12 类是否需要剔除（1=剔除）
int percent[12] = { 0 };  // 12 类的 5x5 区域阈值
int fa_ctl = 0;             // 增强（膨胀）像素数

std::atomic<bool> running{ true };

// ============================================================================
// 阀总数 = 192（与 FPGA 板硬件配置一致）
// ============================================================================
const int VALVE_COUNT = 192;

// ============================================================================
// ===== 用户可配置参数（手改这一段的初值）=====
// ============================================================================
static std::string  g_comPort = "COM8";    // 串口号
static int          g_baudRate = 115200;    // 波特率
static unsigned short g_duration = 30;        // 吹气时长（毫秒）
static unsigned short g_delay = 1;         // 延时（毫秒）
static int          g_frontOffset = 2;         // 前偏移（左侧裁剪像素数）
static int          g_backOffset = 0;         // 后偏移（右侧裁剪像素数）
// ============================================================================

// 串口句柄
static HANDLE g_hSerial = INVALID_HANDLE_VALUE;

// ============================================================================
// 配置函数（可在 Start_send 之前调用，覆盖默认值）
// ============================================================================
void SetSerialPort(const std::string& comPort, int baudRate) {
    g_comPort = comPort;
    g_baudRate = baudRate;
}

void SetEjectParams(unsigned short durationMs, unsigned short delayMs) {
    g_duration = durationMs;
    g_delay = delayMs;
}

void SetPixelOffset(int frontOffset, int backOffset) {
    g_frontOffset = frontOffset;
    g_backOffset = backOffset;
}

// ============================================================================
// Start_send: 打开串口（替换原 UDP 初始化）
// ============================================================================
bool Start_send() {
    // COM10 以上需要用 "\\\\.\\COM10" 形式
    std::string fullPort = "\\\\.\\" + g_comPort;

    g_hSerial = CreateFileA(
        fullPort.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,                  // 独占
        NULL,
        OPEN_EXISTING,
        0,                  // 同步 IO
        NULL);

    if (g_hSerial == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        std::cerr << "[Eject] 打开串口失败: " << g_comPort
            << " (错误码 " << err << ")" << std::endl;
        return false;
    }

    // 配置串口参数
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(g_hSerial, &dcb)) {
        std::cerr << "[Eject] GetCommState 失败" << std::endl;
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }
    dcb.BaudRate = g_baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(g_hSerial, &dcb)) {
        std::cerr << "[Eject] SetCommState 失败" << std::endl;
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    // 写超时设置
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(g_hSerial, &timeouts);

    // 清空缓冲
    PurgeComm(g_hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    std::cout << "[Eject] 串口打开成功: " << g_comPort
        << " @ " << g_baudRate << " bps" << std::endl;
    std::cout << "[Eject] 像素偏移: 前=" << g_frontOffset
        << " 后=" << g_backOffset
        << " 有效范围=[" << g_frontOffset << "," << (g_Samples - g_backOffset) << "]"
        << std::endl;
    std::cout << "[Eject] 阀总数=" << VALVE_COUNT
        << " 吹气=" << g_duration << "ms 延时=" << g_delay << "ms" << std::endl;
    return true;
}

// ============================================================================
// Stop_send: 关闭串口
// ============================================================================
void Stop_send() {
    if (g_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        std::cout << "[Eject] 串口已关闭" << std::endl;
    }
}

// ============================================================================
// OpenAir: 发送一条 7 字节阀控命令（私有）
//   startIndex / endIndex: 阀号（1~192）
//   duration            : 吹气时长 ms
//   delay               : 延时 ms
// ============================================================================
static void OpenAir(int startIndex, int endIndex,
    unsigned short duration, unsigned short delay)
{
    if (g_hSerial == INVALID_HANDLE_VALUE) return;

    // 范围保护（虽然不会超，但加层保险）
    if (startIndex < 1)   startIndex = 1;
    if (endIndex > 255) endIndex = 255;
    if (startIndex > endIndex) return;

    unsigned char message[7];
    message[0] = 0xFF;
    message[1] = (unsigned char)startIndex;
    message[2] = (unsigned char)endIndex;
    message[3] = (unsigned char)(delay >> 8);
    message[4] = (unsigned char)(delay & 0xFF);
    message[5] = (unsigned char)(duration >> 8);
    message[6] = (unsigned char)(duration & 0xFF);

    DWORD bytesWritten = 0;
    if (!WriteFile(g_hSerial, message, 7, &bytesWritten, NULL)) {
        std::cerr << "[Eject] 串口写入失败, 错误码: "
            << GetLastError() << std::endl;
    }
}

// ============================================================================
// send_serial_valve_cmd: 把一批分选结果转换为串口命令
//
// 算法：
//   1) 50 帧取并集 → 得到 192 阀的触发状态
//      （只要这 50 帧内任何一帧的对应像素被标记，该阀就触发）
//   2) 把触发的阀号合并成连续区间
//      例如 [3,4,5,8,9,15] → 区间 (3,5) (8,9) (15,15) 三段
//   3) 每个区间发一条 7 字节命令
// ============================================================================
void send_serial_valve_cmd(char* tags_new)
{
    const int ROWS = dealCount;
    const int COLS = g_Samples;

    // ===== 计算有效像素范围（基于前偏移和后偏移）=====
    int x_start = g_frontOffset;                // 起始像素（含）
    int x_end = g_Samples - g_backOffset;     // 结束像素（不含）

    // 范围合法性检查
    if (x_start < 0)        x_start = 0;
    if (x_end > g_Samples) x_end = g_Samples;
    if (x_end <= x_start) {
        std::cerr << "[Eject] 像素偏移无效, 无法发送命令" << std::endl;
        return;
    }

    // 有效像素总宽度
    int x_range = x_end - x_start;

    // ===== Step 1: 50 帧并集 → 192 阀触发状态 =====
    bool valve_trigger[VALVE_COUNT] = { false };

    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        // 第 fa 号阀（0-base 内部索引）对应的像素区间
        // 用整数除法保证不重叠不漏
        // 例：x_range=638, fa=0 → [2, 5+]   fa=1 → [5, 8+]
        int fa_x1 = x_range * fa / VALVE_COUNT + x_start;
        int fa_x2 = x_range * (fa + 1) / VALVE_COUNT + x_start;
        if (fa_x2 >= COLS) fa_x2 = COLS - 1;

        // 在该像素区间内、所有帧中查找是否有触发
        bool hit = false;
        for (int y = 0; y < ROWS && !hit; y++) {
            const char* row = tags_new + y * COLS;
            for (int m = fa_x1; m <= fa_x2; m++) {
                if (row[m] == 1) { hit = true; break; }
            }
        }
        valve_trigger[fa] = hit;
    }

    // ===== Step 2: 合并连续阀号成区间 → 发送命令 =====
    // 注意：阀号外发时从 1 开始（1~192），而内部索引从 0 开始（0~191）
    // 所以发送时要 +1
    int i = 0;
    int cmdCount = 0;
    while (i < VALVE_COUNT) {
        if (!valve_trigger[i]) { i++; continue; }

        int segStart = i;
        int segEnd = i;
        // 向右扩展找连续段
        while (segEnd + 1 < VALVE_COUNT && valve_trigger[segEnd + 1]) {
            segEnd++;
        }

        // 发送（内部 0-base → 外部 1-base，所以 +1）
        OpenAir(segStart + 1, segEnd + 1, g_duration, g_delay);
        cmdCount++;

        i = segEnd + 1;
    }

    // 调试输出（生产环境可注释掉）
    // if (cmdCount > 0) {
    //     std::cout << "[Eject] 本批发送 " << cmdCount << " 条命令" << std::endl;
    // }
}

// ============================================================================
// EnhanceTags: 十字膨胀（保留原算法）
//   每个 tag==1 的位置，向上下左右各扩展 numAdd 个像素
// ============================================================================
void EnhanceTags(char* tags_new, int rows, int cols, int numAdd, char* tags_Enhance)
{
    if (numAdd == 0) return;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (tags_new[i * cols + j] == 1) {
                int up = (std::max)(i - numAdd, 0);
                int down = (std::min)(i + numAdd, rows - 1);
                for (int k = up; k <= down; k++) {
                    tags_Enhance[k * cols + j] = 1;
                }
                int left = (std::max)(j - numAdd, 0);
                int right = (std::min)(j + numAdd, cols - 1);
                for (int l = left; l <= right; l++) {
                    tags_Enhance[i * cols + l] = 1;
                }
            }
        }
    }
}

// ============================================================================
// ControlValvesWithRows: 主入口
//   保持原签名不变，main.cpp 调用方式不改
// ============================================================================
void ControlValvesWithRows(char* tags, int img_width)
{
    const int ROWS = dealCount;
    const int COLS = g_Samples;
    const int total = ROWS * COLS;
    if (total <= 0) return;

    int numAdd = fa_ctl;

    char* tags_new = new char[total];
    char* tags_5x5 = new char[total]();

    // ---- Step 1: 根据 sel_type[] 过滤要剔除的种类 ----
    for (int i = 0; i < total; i++) {
        int tag_value = (unsigned char)tags[i];
        // tag_value 是分类 ID（1~12），sel_type[tag_value-1]==1 表示该类要剔除
        tags_new[i] = (tag_value > 0 && tag_value <= 12 &&
            sel_type[tag_value - 1] == 1) ? 1 : 0;
    }

    // ---- Step 2: 5x5 区域和过滤（用积分图加速）----
    int* integral = new int[(ROWS + 1) * (COLS + 1)]();
    for (int r = 1; r <= ROWS; r++) {
        for (int c = 1; c <= COLS; c++) {
            integral[r * (COLS + 1) + c] =
                tags_new[(r - 1) * COLS + (c - 1)]
                + integral[(r - 1) * (COLS + 1) + c]
                + integral[r * (COLS + 1) + (c - 1)]
                - integral[(r - 1) * (COLS + 1) + (c - 1)];
        }
    }
    for (int row = 2; row < ROWS - 2; row++) {
        for (int col = 2; col < COLS - 2; col++) {
            int idx = row * COLS + col;
            if (!tags_new[idx]) { tags_5x5[idx] = 0; continue; }
            int sum = integral[(row + 3) * (COLS + 1) + (col + 3)]
                - integral[(row - 2) * (COLS + 1) + (col + 3)]
                - integral[(row + 3) * (COLS + 1) + (col - 2)]
                + integral[(row - 2) * (COLS + 1) + (col - 2)];
            tags_5x5[idx] = (sum > percent[0]) ? 1 : 0;
        }
    }
    delete[] integral;

    // ---- Step 3: 阀位增强（可选膨胀）----
    char* tags_final = tags_5x5;
    char* tags_Enhance = nullptr;
    if (numAdd > 0) {
        tags_Enhance = new char[total]();
        EnhanceTags(tags_5x5, ROWS, COLS, numAdd, tags_Enhance);
        tags_final = tags_Enhance;
    }

    // ---- Step 4: 串口发送阀控命令 ----
    send_serial_valve_cmd(tags_final);

    // 释放临时缓冲
    if (tags_Enhance) delete[] tags_Enhance;
    delete[] tags_new;
    delete[] tags_5x5;
}

// ============================================================================
// UDP_receive_thread: 接收操作屏下发的参数
//   保留原协议（操作屏仍走 UDP 8080 端口）
//   接收的字段：sel_type[] / percent[] / fa_ctl
// ============================================================================
typedef unsigned char byte;

void UDP_receive_thread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Eject] WSAStartup 失败" << std::endl;
        return;
    }

    SOCKET udpReceive = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpReceive == INVALID_SOCKET) {
        std::cerr << "[Eject] 接收套接字创建失败" << std::endl;
        WSACleanup();
        return;
    }

    int reuseAddr = 1;
    setsockopt(udpReceive, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    sockaddr_in serverAddr1{};
    serverAddr1.sin_family = AF_INET;
    serverAddr1.sin_port = htons(8080);
    serverAddr1.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(udpReceive, reinterpret_cast<sockaddr*>(&serverAddr1),
        sizeof(serverAddr1)) == SOCKET_ERROR) {
        std::cerr << "[Eject] UDP 绑定失败" << std::endl;
        closesocket(udpReceive);
        WSACleanup();
        return;
    }

    std::vector<byte> buf(1024);
    while (running) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        int recvLen = recvfrom(udpReceive,
            reinterpret_cast<char*>(buf.data()),
            (int)buf.size(), 0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen);
        if (recvLen > 0 && buf[0] == 2 && buf[1] == 0x2a) {
            sendok = buf[5];
            // 注意：原 UDP 协议下发的 start_line/end_line 在串口模式下不再使用
            //      像素偏移由 SetPixelOffset 设置
            for (int i = 0; i < 12; i++) {
                sel_type[i] = buf[12 + i * 2];
                percent[i] = buf[13 + i * 2];
            }
            fa_ctl = buf[40];
        }
    }

    closesocket(udpReceive);
    WSACleanup();
}