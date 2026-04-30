// ============================================================================
// 文件名：Eject.h
// 作用  ：阀控通信模块（串口版本）
//
// 通信方式：串口（COM 口）
// 协议   ：7 字节命令
//          [0xFF] [start_idx] [end_idx] [delay_h] [delay_l] [duration_h] [duration_l]
//          - start_idx / end_idx : 阀号（1~192，从 1 开始）
//          - delay              : 延时 ms（高低字节，big-endian）
//          - duration           : 吹气时长 ms（高低字节，big-endian）
//
// 阀位映射：
//   有效像素范围 = [前偏移, 640 - 后偏移]
//   阀间距       = 有效像素宽度 / 192
//   第 i 号阀对应像素区间 [前偏移 + (i-1)*间距, 前偏移 + i*间距]
// ============================================================================
#pragma once
#include <string>

// 心跳/同步字节（保留原变量，UDP 接收线程仍可使用）
extern char firstByte;

// ============== 主入口（保持原接口，main.cpp 不用改）==============
// 处理一批分选结果（dealCount 帧 × g_Samples 像素），转换为串口命令并发送
void ControlValvesWithRows(char* tags, int img_width);

// 阀位增强（十字膨胀），保留原算法
void EnhanceTags(char* tags_new, int rows, int cols, int numAdd, char* tags_Enhance);

// ============== 串口控制（替换原 UDP 接口）==============
// 打开串口（替换原 UDP 的 Start_send 同名函数，main.cpp 中调用名不变）
bool Start_send();

// 关闭串口（程序退出时调用）
void Stop_send();

// ============== 可配置参数（在 Start_send 之前设置）==============
// 设置串口端口与波特率
void SetSerialPort(const std::string& comPort, int baudRate);

// 设置吹气参数
//   durationMs : 吹气时长（毫秒）
//   delayMs    : 延时（毫秒）
void SetEjectParams(unsigned short durationMs, unsigned short delayMs);

// 设置像素裁剪偏移
//   frontOffset : 前偏移（像素，从图像左侧裁掉多少）
//   backOffset  : 后偏移（像素，从图像右侧裁掉多少）
// 例如 frontOffset=2, backOffset=0 → 有效范围 2~640
//      frontOffset=2, backOffset=5 → 有效范围 2~635
void SetPixelOffset(int frontOffset, int backOffset);

// ============== UDP 参数接收线程（仍保留，操作屏可继续用 UDP 下发参数）==============
void UDP_receive_thread();

// ============== 内部使用 ==============
// 把一批分选结果转换为串口命令（外部一般不直接调用）
void send_serial_valve_cmd(char* tags_new);