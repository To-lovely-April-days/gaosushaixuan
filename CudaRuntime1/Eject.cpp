#include "Eject.h"
#include <vector>
#include <WinSock2.h>
#include <iostream>
#include <ws2tcpip.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

//相机参数
extern int dealCount;
extern int g_Samples;
extern int frameq;

//心跳
char firstByte;

// 全局变量
int sendok = 0;
int start_line = 0;
int end_line = 0;
int sel_type[12] = { 0 };
int percent[12] = { 0 };
int fa_ctl = 0;

std::atomic<bool> running{ true }; // 控制 UDP 接收线程的标志位

const int pj_line = 4;
const int eject_row = dealCount;
int eject_col = g_Samples;

//unsigned char* tags_PJ = new unsigned char[eject_row * eject_col];
//char* tags_new = new  char[eject_row * eject_col];
//unsigned char* tags_5x5 = new unsigned char[eject_row * eject_col];
//
//void ControlValvesWithRows(char* tags, int img_width) {
//	const int ROWS = eject_row;
//	const int COLS = eject_col;
//
//	int numAdd = fa_ctl;  // 这个变量假设已定义
//
//	//拼接
//	//int lf = (eject_row - pj_line) * eject_col;
//	//int pf = pj_line * eject_col;
//	//memcpy(tags_PJ, tags_PJ + lf, pf);
//	//memcpy(tags_PJ + pf, tags, lf);
//
//	//// 填充tags_new数组
//	//memset(tags_new, 0, eject_row * eject_col);
//	int total = ROWS * COLS;
//	for (size_t i = 0; i < total; i++)
//	{
//		int tag_value = tags[i]; // 提前获取 tags_PJ 中的值
//		if (tag_value > 0) {
//			if (sel_type[tag_value - 1] == 1)
//			{
//				tags_new[i] = 1;
//			}
//			else {
//				tags_new[i] = 0;
//			}
//		}
//		else {
//			tags_new[i] = 0;
//		}
//	}
//
//
//	//for (int row = 0; row < ROWS; row++)
//	//{
//	//	for (int col = 0; col < COLS; col++)
//	//	{
//	//		int tag_value = tags_PJ[row * COLS + col]; // 提前获取 tags_PJ 中的值
//	//		if (tag_value > 0) {
//	//			if (sel_type[tag_value - 1] == 1)
//	//			{
//	//				tags_new[row * COLS + (COLS - 1 - col)] = 1;
//	//			}
//	//			/*	else {
//	//					tags_new[row * COLS + (COLS - 1 - col)] = 0;
//	//				}*/
//	//		}
//	//		//else {
//	//		//	tags_new[row * COLS + (COLS - 1 - col)] = 0;
//	//		//}
//	//	}
//	//}
//	//打印 tags_new
//	/*std::string dis = "";
//	int couttt1 = 0;
//	for (size_t i = pj_line; i < ROWS - 2; i++)
//	{
//		couttt1 = 0;
//		dis = ("enter1:");
//		for (size_t pair = 2; pair < Samples - 2; pair++)
//		{
//			if (tags_new[i * Samples + pair] == 1)
//			{
//				couttt1++;
//				dis += std::to_string(tags_new[i * Samples + pair]) + " ";
//			}
//		}
//		dis += ("\n");
//		if (couttt1 > 0)
//		{
//			printf(dis.c_str());
//			break;
//		}
//	}*/
//
//	//激活帧   固定用5x5，不要改
//	//for (int row = 2; row < ROWS - 2; row++) {
//	//	for (int col = 2; col < COLS - 2; col++) {
//	//		/*int num =  tags_new[(row - 2) * COLS + (col - 2)] + tags_new[(row - 2) * COLS + (col - 1)] + tags_new[(row - 2) * COLS + col] +
//	//			tags_new[(row - 2) * COLS + (col + 1)] + tags_new[(row - 2) * COLS + (col + 2)] +
//	//			tags_new[(row - 1) * COLS + (col - 2)] + tags_new[(row - 1) * COLS + (col - 1)] + tags_new[(row - 1) * COLS + col] +
//	//			tags_new[(row - 1) * COLS + (col + 1)] + tags_new[(row - 1) * COLS + (col + 2)] +
//	//			tags_new[row * COLS + (col - 2)] + tags_new[row * COLS + (col - 1)] + tags_new[row * COLS + col] +
//	//			tags_new[row * COLS + (col + 1)] + tags_new[row * COLS + (col + 2)] +
//	//			tags_new[(row + 1) * COLS + (col - 2)] + tags_new[(row + 1) * COLS + (col - 1)] + tags_new[(row + 1) * COLS + col] +
//	//			tags_new[(row + 1) * COLS + (col + 1)] + tags_new[(row + 1) * COLS + (col + 2)] +
//	//			tags_new[(row + 2) * COLS + (col - 2)] + tags_new[(row + 2) * COLS + (col - 1)] + tags_new[(row + 2) * COLS + col] +
//	//			tags_new[(row + 2) * COLS + (col + 1)] + tags_new[(row + 2) * COLS + (col + 2)];*/
//
//	//		if (tags_new[row * COLS + col] == 1 /*&& num > percent[0]*/) {
//	//			tags_5x5[row * COLS + col] = 1;
//	//		}
//	//		else {
//	//			tags_5x5[row * COLS + col] = 0;
//	//		}
//	//	}
//	//}
//
//	// 假设 ROWS 和 COLS 已经定义
//// 创建一个缓存数组，存储每行 5 个元素的和
//	//int row_sums[54][640];
//
//	//// 预计算每行 5 个相邻元素的和
//	//for (int row = 0; row < ROWS; row++) {
//	//	for (int col = 2; col < COLS - 2; col++) {
//	//		row_sums[row][col] = tags_new[row * COLS + (col - 2)] + tags_new[row * COLS + (col - 1)] + tags_new[row * COLS + col] +
//	//			tags_new[row * COLS + (col + 1)] + tags_new[row * COLS + (col + 2)];
//	//	}
//	//}
//
//	//// 计算每个 5x5 区域的和
//	//for (int row = 2; row < ROWS - 2; row++) {
//	//	for (int col = 2; col < COLS - 2; col++) {
//	//		int num = 0;
//
//	//		// 使用已计算的行和来减少重复计算
//	//		for (int i = -2; i <= 2; i++) {
//	//			// 计算周围的列
//	//			num += row_sums[row + i][col];
//	//		}
//
//	//		// 判断并更新 tags_5x5
//	//		if (tags_new[row * COLS + col] == 1 && num > percent[0]) {
//	//			tags_5x5[row * COLS + col] = 1;
//	//		}
//	//		else {
//	//			tags_5x5[row * COLS + col] = 0;
//	//		}
//	//	}
//	//}
//
//
//
//	//打印 tags_5x5
//	//std::string dis = "";
//	/*int couttt2 = 0;
//	for (size_t i = pj_line; i < ROWS - 2; i++)
//	{
//		couttt2 = 0;
//		dis = ("enter2:");
//		for (size_t pair = 2; pair < Samples - 2; pair++)
//		{
//			if (tags_5x5[i * Samples + pair] == 1)
//			{
//				couttt2++;
//				dis += std::to_string(tags_5x5[i * Samples + pair]) + " ";
//			}
//		}
//		dis += ("\n");
//		if (couttt2 > 0)
//		{
//			printf(dis.c_str());
//			break;
//		}
//	}
//	if (couttt1 > 0 && couttt2 == 0)
//	{
//		printf("enter2:--------------------\n");
//	}*/
//	char* tags_Enhance = new  char[ROWS * COLS];
//	// 调用增强函数
//	EnhanceTags(tags, ROWS, COLS, numAdd, tags_Enhance);
//
//	// 发送包
//	if (numAdd == 0)
//		send_pkg_256(tags);
//	else
//		send_pkg_256(tags_Enhance);
//
//
//	delete tags_Enhance;
//	// 清理动态分配的内存
//	//delete[] tags_buf;
//	//delete[] tags_pingjie;
//	/*delete[] tags_new;
//	delete[] tags_5x5;*/
//}

//修改5*5
void ControlValvesWithRows(char* tags, int img_width) {
	const int ROWS = eject_row;
	const int COLS = eject_col;
	const int total = ROWS * COLS;


	char* tags_PJ = new char[total];
	char* tags_new = new char[total];
	char* tags_5x5 = new char[total]();

	int numAdd = fa_ctl;

	// 复制输入数据到本地缓冲区
	memcpy(tags_PJ, tags, total * sizeof(char));

	// 预处理生成tags_new
	for (size_t i = 0; i < total; ++i) {
		int tag_value = tags_PJ[i];
		tags_new[i] = (tag_value > 0 && sel_type[tag_value - 1] == 1) ? 1 : 0;
	}

	// 计算积分图优化区域和计算
	int* integral = new int[(ROWS + 1) * (COLS + 1)]{ 0 };
	for (int i = 1; i <= ROWS; ++i) {
		for (int j = 1; j <= COLS; ++j) {
			integral[i * (COLS + 1) + j] = tags_new[(i - 1) * COLS + (j - 1)]
				+ integral[(i - 1) * (COLS + 1) + j]
				+ integral[i * (COLS + 1) + (j - 1)]
				- integral[(i - 1) * (COLS + 1) + (j - 1)];
		}
	}

	// 并行处理
#pragma omp parallel for collapse(2)
	for (int row = 2; row < ROWS - 2; ++row) {
		for (int col = 2; col < COLS - 2; ++col) {
			const int idx = row * COLS + col;
			if (!tags_new[idx]) {
				tags_5x5[idx] = 0;
				continue;
			}

			// 使用积分图快速计算5x5区域和
			const int sum = integral[(row + 3) * (COLS + 1) + (col + 3)]
				- integral[(row - 2) * (COLS + 1) + (col + 3)]
				- integral[(row + 3) * (COLS + 1) + (col - 2)]
				+ integral[(row - 2) * (COLS + 1) + (col - 2)];

			tags_5x5[idx] = (sum > percent[0]) ? 1 : 0;
		}
	}

	delete[] integral;

	// 后处理增强,阀启动大小
	char* tags_Enhance = new char[total];
	EnhanceTags(tags_5x5, ROWS, COLS, numAdd, tags_Enhance);

	// 选择发送结果
	send_pkg_256(numAdd ? tags_Enhance : tags_5x5);

	// 清理所有分配的内存
	delete[] tags_Enhance;
	delete[] tags_PJ;
	delete[] tags_new;
	delete[] tags_5x5;
}

void EnhanceTags(char* tags_new, int rows, int cols, int numAdd, char* tags_Enhance) {
	if (numAdd == 0) return;

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			if (tags_new[i * cols + j] == 1) {
				// 上下增强
				int up = max(i - numAdd, 0);  // 上界
				int down = min(i + numAdd, rows - 1);  // 下界
				for (int k = up; k <= down; k++) {
					tags_Enhance[k * cols + j] = 1;
				}
				// 左右增强
				int left = max(j - numAdd, 0);  // 左界
				int right = min(j + numAdd, cols - 1);  // 右界
				for (int l = left; l <= right; l++) {
					tags_Enhance[i * cols + l] = 1;
				}
			}
		}
	}

}


typedef unsigned char byte;

class ByteBuilder {
private:
	std::vector<byte> buffer_; // 数据缓冲区

public:
	// 清空缓冲区
	void Clear() {
		buffer_.clear();
	}

	// 添加一个字节到缓冲区
	void Add(byte value) {
		buffer_.push_back(value);
	}

	// 获取缓冲区的数据指针
	const byte* ToArray() const {
		return buffer_.data();
	}

	// 获取缓冲区的大小
	int Count() const {
		return static_cast<int>(buffer_.size());
	}
};

int valve_on = 1;//阀控制
char data_num = 0;

//参数说明 50 = dealCount

// UDP 发送函数
// 发送函数，用来给电路板发送初始的信息
SOCKET udpSend;
sockaddr_in serverAddr;

void Start_send() {
	// 初始化 Winsock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup 失败" << std::endl;
		return;
	}

	// 创建 UDP 套接字
	udpSend = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpSend == INVALID_SOCKET) {
		std::cerr << "套接字创建失败" << std::endl;
		WSACleanup();
		return;
	}
	int reuseAddr = 1;
	setsockopt(udpSend, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
	// 设置目标地址
	//sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(8080); // 端口号 8080
	inet_pton(AF_INET, "192.168.1.65", &serverAddr.sin_addr); // IP 地址

	// 构建发送数据
	ByteBuilder builder_send;
	builder_send.Clear();
	builder_send.Add(0x1); // 通信 byte0
	builder_send.Add(0); // 帧数 byte1
	int data1 = dealCount;
	byte data2 = static_cast<byte>(data1 / 256);
	builder_send.Add(data2); // 帧数 byte2
	data2 = static_cast<byte>(data1 % 256);
	builder_send.Add(data2); // 帧数 byte3
	data1 = 50;
	data2 = static_cast<byte>(data1 / 256);
	builder_send.Add(data2); // 阀延时时间 byte4
	data2 = static_cast<byte>(data1 % 256);
	builder_send.Add(data2); // 阀延时时间 byte5
	data1 = 1000000 / frameq;//根据相机帧频变化 1000000/帧频  比如：1000000/2300 = 434
	data2 = static_cast<byte>(data1 / 256);
	builder_send.Add(data2); // 一帧时间 byte6
	data2 = static_cast<byte>(data1 % 256);
	builder_send.Add(data2); // 一帧时间 byte7
	data2 = static_cast<byte>(valve_on);
	builder_send.Add(data_num); // 信号控制 byte8
	for (int i = 0; i < 23; i++) { // 补 23 个 0
		builder_send.Add(0x0); // 0
	}

	// 发送数据
	int result = sendto(udpSend, reinterpret_cast<const char*>(builder_send.ToArray()), builder_send.Count(), 0,
		reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
	if (result == SOCKET_ERROR) {
		int error = WSAGetLastError();
		std::cerr << "sendto 失败，错误码: " << error << std::endl;
	}
	else
	{
		std::cout << "send ok" << std::endl;
	}

	//closesocket(udpSend);
	//WSACleanup();
}
//SOCKET udpReceive;


void UDP_receive_thread() {
	// 初始化 Winsock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cerr << "WSAStartup 失败" << std::endl;
		return;
	}

	// 创建 UDP 套接字
	SOCKET udpReceive = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpReceive == INVALID_SOCKET) {
		std::cerr << "套接字创建失败" << std::endl;
		WSACleanup();
		return;
	}

	int reuseAddr = 1;
	setsockopt(udpReceive, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

	// 绑定地址和端口
	sockaddr_in serverAddr1;
	serverAddr1.sin_family = AF_INET;
	serverAddr1.sin_port = htons(8080); // 端口号 8080
	serverAddr1.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定到所有可用的网络接口

	if (::bind(udpReceive, reinterpret_cast<sockaddr*>(&serverAddr1), sizeof(serverAddr1)) == SOCKET_ERROR) {
		std::cerr << "绑定套接字失败" << std::endl;
		closesocket(udpReceive);
		WSACleanup();
		return;
	}

	// 接收缓冲区
	std::vector<byte> receiveBytes(1024);

	while (running) {
		sockaddr_in clientAddr;
		int clientAddrLen = sizeof(clientAddr);

		// 接收数据
		int recvLen = recvfrom(udpReceive, reinterpret_cast<char*>(receiveBytes.data()), receiveBytes.size(), 0,
			reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
		if (recvLen > 0) {
			//std::cout << "receive ok" << std::endl;
			// 检查接收到的数据是否有效
			if (receiveBytes[0] == 2 && receiveBytes[1] == 0x2a) { // 固定值

				// 更新全局变量
				sendok = receiveBytes[5];
				start_line = receiveBytes[8] * 256 + receiveBytes[9]; // 像素对应的起始值
				end_line = receiveBytes[10] * 256 + receiveBytes[11]; // 像素对应的结束值

				for (int i = 0; i < 12; i++) {
					sel_type[i] = receiveBytes[12 + i * 2]; // 操作屏 AI 界面，种类选择
					percent[i] = receiveBytes[13 + i * 2]; // 操作屏 AI 界面，百分比选择
				}

				fa_ctl = receiveBytes[40]; // 控制参数
			}
		}
		else {
			std::cerr << "接收数据失败" << std::endl;
		}
	}

	// 关闭套接字
	closesocket(udpReceive);
	WSACleanup();
}


void send_pkg_256(char* tags_new) {
	//// 初始化 Winsock
	//WSADATA wsaData;
	//if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
	//	std::cerr << "WSAStartup 失败" << std::endl;
	//	return;
	//}

	//// 创建 UDP 套接字
	//SOCKET udpSend = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//if (udpSend == INVALID_SOCKET) {
	//	std::cerr << "套接字创建失败" << std::endl;
	//	WSACleanup();
	//	return;
	//}
	//int reuseAddr = 1;
	//setsockopt(udpSend, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
	//// 设置目标地址
	//sockaddr_in serverAddr;
	//serverAddr.sin_family = AF_INET;
	//serverAddr.sin_port = htons(8080); // 端口号 8080
	//inet_pton(AF_INET, "192.168.1.65", &serverAddr.sin_addr); // IP 地址

	// 初始化 fa_valve
	std::vector<byte> fa_valve(eject_row * 32, 0);
	end_line = 593;
	// 计算起始行和结束行
	int x_start_line = eject_col - end_line;
	if (x_start_line >= eject_col) x_start_line = 10;
	start_line = 15;
	int x_end_line = eject_col - start_line;
	if (x_end_line >= eject_col) x_end_line = 620;

	int x_line64 = x_end_line - x_start_line;
	byte bit0 = 0;
	byte bit1 = 0;
	byte bit2 = 0;
	byte bit3 = 0;
	byte bit4 = 0;
	byte bit5 = 0;
	byte bit6 = 0;
	byte bit7 = 0;

	for (int y = 0; y < eject_row; y++) {
		for (int fa = 0; fa < 128; fa++) {
			int fa_x1 = x_line64 * fa;
			fa_x1 = fa_x1 / 128;
			fa_x1 = fa_x1 + x_start_line;
			int fa_x2 = x_line64 * (fa + 1);
			fa_x2 = fa_x2 / 128;
			fa_x2 = fa_x2 + x_start_line;
			int biaoji = 0;
			for (int m = fa_x1; m <= fa_x2; m++) {
				if (tags_new[y * 640 + m] == 1) {  // Flatten the 2D access for tags_new
					biaoji = 1;
					//printf("111");
					break;
				}
			}
			if (fa % 8 == 0) {
				if (biaoji == 0) bit7 = 0;
				else bit7 = 128;
			}
			else if (fa % 8 == 1) {
				if (biaoji == 0) bit6 = 0;
				else bit6 = 64;
			}
			else if (fa % 8 == 2) {
				if (biaoji == 0) bit5 = 0;
				else bit5 = 32;
			}
			else if (fa % 8 == 3) {
				if (biaoji == 0) bit4 = 0;
				else bit4 = 16;
			}
			else if (fa % 8 == 4) {
				if (biaoji == 0) bit3 = 0;
				else bit3 = 8;
			}
			else if (fa % 8 == 5) {
				if (biaoji == 0) bit2 = 0;
				else bit2 = 4;
			}
			else if (fa % 8 == 6) {
				if (biaoji == 0) bit1 = 0;
				else bit1 = 2;
			}
			else if (fa % 8 == 7) {
				if (biaoji == 0) bit0 = 0;
				else bit0 = 1;

				fa_valve[(fa - 7) / 8 + y * 32] = (byte)(bit0 + bit1 + bit2 + bit3 + bit4 + bit5 + bit6 + bit7);
			}
		}
	}

	// 初始化 fa_data_pkg
	std::vector<byte> fa_data_pkg(eject_row * 32 + 96, 0);

	// 填充数据包头
	fa_data_pkg[0] = 0x02;
	fa_data_pkg[1] = 0x1;
	fa_data_pkg[2] = 0x1;
	fa_data_pkg[3] = 0x00;
	fa_data_pkg[4] = 0x00;
	fa_data_pkg[5] = 0x80;
	fa_data_pkg[6] = 0x01;
	fa_data_pkg[7] = 0x00;
	fa_data_pkg[8] = 0x00;
	fa_data_pkg[9] = firstByte;

	// 填充数据包内容
	for (int i = 0; i < eject_row; i++) {
		for (int m = 0; m < 32; m++) {
			fa_data_pkg[32 + i * 32 + m] = fa_valve[m + i * 32];
		}
	}

	// 填充数据包尾部
	for (int m = 0; m < 32; m++) {
		fa_data_pkg[eject_row * 32 + m] = fa_data_pkg[eject_row * 32 + m];
	}
	for (int m = 0; m < 32; m++) {
		fa_data_pkg[eject_row * 32 + 32 + m] = fa_data_pkg[eject_row * 32 + m];
	}


	// 发送数据包
	int result = sendto(udpSend, reinterpret_cast<const char*>(fa_data_pkg.data()), fa_data_pkg.size(), 0,
		reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));



	// 关闭套接字
	//closesocket(udpSend);
	//WSACleanup();
}












//#include "Eject.h"
//#include <vector>
//#include <WinSock2.h>
//#include <iostream>
//#include <ws2tcpip.h>
//#include <string>
//
//#pragma comment(lib, "ws2_32.lib")
//
////相机参数
//extern int dealCount;
//extern int Samples;
//extern int frameq;
//
////心跳
//char firstByte;
//
//// 全局变量
//int sendok = 0;
//int start_line = 0;
//int end_line = 0;
//int sel_type[12] = { 0 };
//int percent[12] = { 0 };
//int fa_ctl = 0;
//
//std::atomic<bool> running{ true }; // 控制 UDP 接收线程的标志位
//
//const int pj_line = 4;
//const int eject_row = dealCount;
//int eject_col = Samples;
//
//unsigned char* tags_PJ = new unsigned char[eject_row * eject_col];
//char* tags_new = new  char[eject_row * eject_col];
//unsigned char* tags_5x5 = new unsigned char[eject_row * eject_col];
//void ControlValvesWithRows(char* tags, int img_width) {
//	const int ROWS = eject_row;
//	const int COLS = eject_col;
//
//	int numAdd = fa_ctl;  // 这个变量假设已定义
//
//	//拼接
//	//int lf = (eject_row - pj_line) * eject_col;
//	//int pf = pj_line * eject_col;
//	//memcpy(tags_PJ, tags_PJ + lf, pf);
//	//memcpy(tags_PJ + pf, tags, lf);
//
//	//// 填充tags_new数组
//	//memset(tags_new, 0, eject_row * eject_col);
//	int total = ROWS * COLS;
//	for (size_t i = 0; i < total; i++)
//	{
//		int tag_value = tags[i]; // 提前获取 tags_PJ 中的值
//		if (tag_value > 0) {
//			if (sel_type[tag_value - 1] == 1)
//			{
//				tags_new[i] = 1;
//			}
//			else {
//				tags_new[i] = 0;
//			}
//		}
//		else {
//			tags_new[i] = 0;
//		}
//	}
//
//
//	//for (int row = 0; row < ROWS; row++)
//	//{
//	//	for (int col = 0; col < COLS; col++)
//	//	{
//	//		int tag_value = tags_PJ[row * COLS + col]; // 提前获取 tags_PJ 中的值
//	//		if (tag_value > 0) {
//	//			if (sel_type[tag_value - 1] == 1)
//	//			{
//	//				tags_new[row * COLS + (COLS - 1 - col)] = 1;
//	//			}
//	//			/*	else {
//	//					tags_new[row * COLS + (COLS - 1 - col)] = 0;
//	//				}*/
//	//		}
//	//		//else {
//	//		//	tags_new[row * COLS + (COLS - 1 - col)] = 0;
//	//		//}
//	//	}
//	//}
//	//打印 tags_new
//	/*std::string dis = "";
//	int couttt1 = 0;
//	for (size_t i = pj_line; i < ROWS - 2; i++)
//	{
//		couttt1 = 0;
//		dis = ("enter1:");
//		for (size_t pair = 2; pair < Samples - 2; pair++)
//		{
//			if (tags_new[i * Samples + pair] == 1)
//			{
//				couttt1++;
//				dis += std::to_string(tags_new[i * Samples + pair]) + " ";
//			}
//		}
//		dis += ("\n");
//		if (couttt1 > 0)
//		{
//			printf(dis.c_str());
//			break;
//		}
//	}*/
//
//	//激活帧   固定用5x5，不要改
//	//for (int row = 2; row < ROWS - 2; row++) {
//	//	for (int col = 2; col < COLS - 2; col++) {
//	//		/*int num =  tags_new[(row - 2) * COLS + (col - 2)] + tags_new[(row - 2) * COLS + (col - 1)] + tags_new[(row - 2) * COLS + col] +
//	//			tags_new[(row - 2) * COLS + (col + 1)] + tags_new[(row - 2) * COLS + (col + 2)] +
//	//			tags_new[(row - 1) * COLS + (col - 2)] + tags_new[(row - 1) * COLS + (col - 1)] + tags_new[(row - 1) * COLS + col] +
//	//			tags_new[(row - 1) * COLS + (col + 1)] + tags_new[(row - 1) * COLS + (col + 2)] +
//	//			tags_new[row * COLS + (col - 2)] + tags_new[row * COLS + (col - 1)] + tags_new[row * COLS + col] +
//	//			tags_new[row * COLS + (col + 1)] + tags_new[row * COLS + (col + 2)] +
//	//			tags_new[(row + 1) * COLS + (col - 2)] + tags_new[(row + 1) * COLS + (col - 1)] + tags_new[(row + 1) * COLS + col] +
//	//			tags_new[(row + 1) * COLS + (col + 1)] + tags_new[(row + 1) * COLS + (col + 2)] +
//	//			tags_new[(row + 2) * COLS + (col - 2)] + tags_new[(row + 2) * COLS + (col - 1)] + tags_new[(row + 2) * COLS + col] +
//	//			tags_new[(row + 2) * COLS + (col + 1)] + tags_new[(row + 2) * COLS + (col + 2)];*/
//
//	//		if (tags_new[row * COLS + col] == 1 /*&& num > percent[0]*/) {
//	//			tags_5x5[row * COLS + col] = 1;
//	//		}
//	//		else {
//	//			tags_5x5[row * COLS + col] = 0;
//	//		}
//	//	}
//	//}
//
//	// 假设 ROWS 和 COLS 已经定义
//// 创建一个缓存数组，存储每行 5 个元素的和
//	//int row_sums[54][640];
//
//	//// 预计算每行 5 个相邻元素的和
//	//for (int row = 0; row < ROWS; row++) {
//	//	for (int col = 2; col < COLS - 2; col++) {
//	//		row_sums[row][col] = tags_new[row * COLS + (col - 2)] + tags_new[row * COLS + (col - 1)] + tags_new[row * COLS + col] +
//	//			tags_new[row * COLS + (col + 1)] + tags_new[row * COLS + (col + 2)];
//	//	}
//	//}
//
//	//// 计算每个 5x5 区域的和
//	//for (int row = 2; row < ROWS - 2; row++) {
//	//	for (int col = 2; col < COLS - 2; col++) {
//	//		int num = 0;
//
//	//		// 使用已计算的行和来减少重复计算
//	//		for (int i = -2; i <= 2; i++) {
//	//			// 计算周围的列
//	//			num += row_sums[row + i][col];
//	//		}
//
//	//		// 判断并更新 tags_5x5
//	//		if (tags_new[row * COLS + col] == 1 && num > percent[0]) {
//	//			tags_5x5[row * COLS + col] = 1;
//	//		}
//	//		else {
//	//			tags_5x5[row * COLS + col] = 0;
//	//		}
//	//	}
//	//}
//
//
//
//	//打印 tags_5x5
//	//std::string dis = "";
//	/*int couttt2 = 0;
//	for (size_t i = pj_line; i < ROWS - 2; i++)
//	{
//		couttt2 = 0;
//		dis = ("enter2:");
//		for (size_t pair = 2; pair < Samples - 2; pair++)
//		{
//			if (tags_5x5[i * Samples + pair] == 1)
//			{
//				couttt2++;
//				dis += std::to_string(tags_5x5[i * Samples + pair]) + " ";
//			}
//		}
//		dis += ("\n");
//		if (couttt2 > 0)
//		{
//			printf(dis.c_str());
//			break;
//		}
//	}
//	if (couttt1 > 0 && couttt2 == 0)
//	{
//		printf("enter2:--------------------\n");
//	}*/
//	char* tags_Enhance = new  char[ROWS * COLS];
//	// 调用增强函数
//	EnhanceTags(tags, ROWS, COLS, numAdd, tags_Enhance);
//
//	// 发送包
//	if (numAdd == 0)
//		send_pkg_256(tags);
//	else
//		send_pkg_256(tags_Enhance);
//
//
//	delete tags_Enhance;
//	// 清理动态分配的内存
//	//delete[] tags_buf;
//	//delete[] tags_pingjie;
//	/*delete[] tags_new;
//	delete[] tags_5x5;*/
//}
//
//
//void EnhanceTags(char* tags_new, int rows, int cols, int numAdd, char* tags_Enhance) {
//	if (numAdd == 0) return;
//
//	for (int i = 0; i < rows; i++) {
//		for (int j = 0; j < cols; j++) {
//			if (tags_new[i * cols + j] == 1) {
//				// 上下增强
//				int up = max(i - numAdd, 0);  // 上界
//				int down = min(i + numAdd, rows - 1);  // 下界
//				for (int k = up; k <= down; k++) {
//					tags_Enhance[k * cols + j] = 1;
//				}
//				// 左右增强
//				int left = max(j - numAdd, 0);  // 左界
//				int right = min(j + numAdd, cols - 1);  // 右界
//				for (int l = left; l <= right; l++) {
//					tags_Enhance[i * cols + l] = 1;
//				}
//			}
//		}
//	}
//
//}
//
//
//typedef unsigned char byte;
//
//class ByteBuilder {
//private:
//	std::vector<byte> buffer_; // 数据缓冲区
//
//public:
//	// 清空缓冲区
//	void Clear() {
//		buffer_.clear();
//	}
//
//	// 添加一个字节到缓冲区
//	void Add(byte value) {
//		buffer_.push_back(value);
//	}
//
//	// 获取缓冲区的数据指针
//	const byte* ToArray() const {
//		return buffer_.data();
//	}
//
//	// 获取缓冲区的大小
//	int Count() const {
//		return static_cast<int>(buffer_.size());
//	}
//};
//
//int valve_on = 1;//阀控制
//char data_num = 0;
//
////参数说明 50 = dealCount
//
//// UDP 发送函数
//// 发送函数，用来给电路板发送初始的信息
//SOCKET udpSend;
//sockaddr_in serverAddr;
//
//void Start_send() {
//	// 初始化 Winsock
//	WSADATA wsaData;
//	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
//		std::cerr << "WSAStartup 失败" << std::endl;
//		return;
//	}
//
//	// 创建 UDP 套接字
//	udpSend = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//	if (udpSend == INVALID_SOCKET) {
//		std::cerr << "套接字创建失败" << std::endl;
//		WSACleanup();
//		return;
//	}
//	int reuseAddr = 1;
//	setsockopt(udpSend, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
//	// 设置目标地址
//	//sockaddr_in serverAddr;
//	serverAddr.sin_family = AF_INET;
//	serverAddr.sin_port = htons(8080); // 端口号 8080
//	inet_pton(AF_INET, "192.168.1.65", &serverAddr.sin_addr); // IP 地址
//
//	// 构建发送数据
//	ByteBuilder builder_send;
//	builder_send.Clear();
//	builder_send.Add(0x1); // 通信 byte0
//	builder_send.Add(0); // 帧数 byte1
//	int data1 = dealCount;
//	byte data2 = static_cast<byte>(data1 / 256);
//	builder_send.Add(data2); // 帧数 byte2
//	data2 = static_cast<byte>(data1 % 256);
//	builder_send.Add(data2); // 帧数 byte3
//	data1 = 50;
//	data2 = static_cast<byte>(data1 / 256);
//	builder_send.Add(data2); // 阀延时时间 byte4
//	data2 = static_cast<byte>(data1 % 256);
//	builder_send.Add(data2); // 阀延时时间 byte5
//	data1 = 1000000 / frameq;//根据相机帧频变化 1000000/帧频  比如：1000000/2300 = 434
//	data2 = static_cast<byte>(data1 / 256);
//	builder_send.Add(data2); // 一帧时间 byte6
//	data2 = static_cast<byte>(data1 % 256);
//	builder_send.Add(data2); // 一帧时间 byte7
//	data2 = static_cast<byte>(valve_on);
//	builder_send.Add(data_num); // 信号控制 byte8
//	for (int i = 0; i < 23; i++) { // 补 23 个 0
//		builder_send.Add(0x0); // 0
//	}
//
//	// 发送数据
//	int result = sendto(udpSend, reinterpret_cast<const char*>(builder_send.ToArray()), builder_send.Count(), 0,
//		reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
//	if (result == SOCKET_ERROR) {
//		int error = WSAGetLastError();
//		std::cerr << "sendto 失败，错误码: " << error << std::endl;
//	}
//	else
//	{
//		std::cout << "send ok" << std::endl;
//	}
//
//	//closesocket(udpSend);
//	//WSACleanup();
//}
////SOCKET udpReceive;
//
//
//void UDP_receive_thread() {
//	// 初始化 Winsock
//	WSADATA wsaData;
//	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
//		std::cerr << "WSAStartup 失败" << std::endl;
//		return;
//	}
//
//	// 创建 UDP 套接字
//	SOCKET udpReceive = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//	if (udpReceive == INVALID_SOCKET) {
//		std::cerr << "套接字创建失败" << std::endl;
//		WSACleanup();
//		return;
//	}
//
//	int reuseAddr = 1;
//	setsockopt(udpReceive, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
//
//	// 绑定地址和端口
//	sockaddr_in serverAddr1;
//	serverAddr1.sin_family = AF_INET;
//	serverAddr1.sin_port = htons(8080); // 端口号 8080
//	serverAddr1.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定到所有可用的网络接口
//
//	if (::bind(udpReceive, reinterpret_cast<sockaddr*>(&serverAddr1), sizeof(serverAddr1)) == SOCKET_ERROR) {
//		std::cerr << "绑定套接字失败" << std::endl;
//		closesocket(udpReceive);
//		WSACleanup();
//		return;
//	}
//
//	// 接收缓冲区
//	std::vector<byte> receiveBytes(1024);
//
//	while (running) {
//		sockaddr_in clientAddr;
//		int clientAddrLen = sizeof(clientAddr);
//
//		// 接收数据
//		int recvLen = recvfrom(udpReceive, reinterpret_cast<char*>(receiveBytes.data()), receiveBytes.size(), 0,
//			reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
//		if (recvLen > 0) {
//			//std::cout << "receive ok" << std::endl;
//			// 检查接收到的数据是否有效
//			if (receiveBytes[0] == 2 && receiveBytes[1] == 0x2a) { // 固定值
//
//				// 更新全局变量
//				sendok = receiveBytes[5];
//				start_line = receiveBytes[8] * 256 + receiveBytes[9]; // 像素对应的起始值
//				end_line = receiveBytes[10] * 256 + receiveBytes[11]; // 像素对应的结束值
//
//				for (int i = 0; i < 12; i++) {
//					sel_type[i] = receiveBytes[12 + i * 2]; // 操作屏 AI 界面，种类选择
//					percent[i] = receiveBytes[13 + i * 2]; // 操作屏 AI 界面，百分比选择
//				}
//
//				fa_ctl = receiveBytes[40]; // 控制参数
//			}
//		}
//		else {
//			std::cerr << "接收数据失败" << std::endl;
//		}
//	}
//
//	// 关闭套接字
//	closesocket(udpReceive);
//	WSACleanup();
//}
//
//
//void send_pkg_256(char* tags_new) {
//
//
//	//// 初始化 Winsock
//	//WSADATA wsaData;
//	//if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
//	//	std::cerr << "WSAStartup 失败" << std::endl;
//	//	return;
//	//}
//
//	//// 创建 UDP 套接字
//	//SOCKET udpSend = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//	//if (udpSend == INVALID_SOCKET) {
//	//	std::cerr << "套接字创建失败" << std::endl;
//	//	WSACleanup();
//	//	return;
//	//}
//	//int reuseAddr = 1;
//	//setsockopt(udpSend, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));
//	//// 设置目标地址
//	//sockaddr_in serverAddr;
//	//serverAddr.sin_family = AF_INET;
//	//serverAddr.sin_port = htons(8080); // 端口号 8080
//	//inet_pton(AF_INET, "192.168.1.65", &serverAddr.sin_addr); // IP 地址
//
//	// 初始化 fa_valve
//	std::vector<byte> fa_valve(eject_row * 32, 0);
//	end_line = 593;
//	// 计算起始行和结束行
//	int x_start_line = eject_col - end_line;
//	if (x_start_line >= eject_col) x_start_line = 10;
//	start_line = 15;
//	int x_end_line = eject_col - start_line;
//	if (x_end_line >= eject_col) x_end_line = 620;
//
//	int x_line64 = x_end_line - x_start_line;
//	byte bit0 = 0;
//	byte bit1 = 0;
//	byte bit2 = 0;
//	byte bit3 = 0;
//	byte bit4 = 0;
//	byte bit5 = 0;
//	byte bit6 = 0;
//	byte bit7 = 0;
//
//	for (int y = 0; y < eject_row; y++) {
//		for (int fa = 0; fa < 128; fa++) {
//			int fa_x1 = x_line64 * fa;
//			fa_x1 = fa_x1 / 128;
//			fa_x1 = fa_x1 + x_start_line;
//			int fa_x2 = x_line64 * (fa + 1);
//			fa_x2 = fa_x2 / 128;
//			fa_x2 = fa_x2 + x_start_line;
//			int biaoji = 0;
//			for (int m = fa_x1; m <= fa_x2; m++) {
//				if (tags_new[y * 640 + m] == 1) {  // Flatten the 2D access for tags_new
//					biaoji = 1;
//					//printf("111");
//					break;
//				}
//			}
//			if (fa % 8 == 0) {
//				if (biaoji == 0) bit7 = 0;
//				else bit7 = 128;
//			}
//			else if (fa % 8 == 1) {
//				if (biaoji == 0) bit6 = 0;
//				else bit6 = 64;
//			}
//			else if (fa % 8 == 2) {
//				if (biaoji == 0) bit5 = 0;
//				else bit5 = 32;
//			}
//			else if (fa % 8 == 3) {
//				if (biaoji == 0) bit4 = 0;
//				else bit4 = 16;
//			}
//			else if (fa % 8 == 4) {
//				if (biaoji == 0) bit3 = 0;
//				else bit3 = 8;
//			}
//			else if (fa % 8 == 5) {
//				if (biaoji == 0) bit2 = 0;
//				else bit2 = 4;
//			}
//			else if (fa % 8 == 6) {
//				if (biaoji == 0) bit1 = 0;
//				else bit1 = 2;
//			}
//			else if (fa % 8 == 7) {
//				if (biaoji == 0) bit0 = 0;
//				else bit0 = 1;
//
//				fa_valve[(fa - 7) / 8 + y * 32] = (byte)(bit0 + bit1 + bit2 + bit3 + bit4 + bit5 + bit6 + bit7);
//			}
//		}
//	}
//
//	// 初始化 fa_data_pkg
//	std::vector<byte> fa_data_pkg(eject_row * 32 + 96, 0);
//
//	// 填充数据包头
//	fa_data_pkg[0] = 0x02;
//	fa_data_pkg[1] = 0x1;
//	fa_data_pkg[2] = 0x1;
//	fa_data_pkg[3] = 0x00;
//	fa_data_pkg[4] = 0x00;
//	fa_data_pkg[5] = 0x80;
//	fa_data_pkg[6] = 0x01;
//	fa_data_pkg[7] = 0x00;
//	fa_data_pkg[8] = 0x00;
//	fa_data_pkg[9] = firstByte;
//
//	// 填充数据包内容
//	for (int i = 0; i < eject_row; i++) {
//		for (int m = 0; m < 32; m++) {
//			fa_data_pkg[32 + i * 32 + m] = fa_valve[m + i * 32];
//		}
//	}
//
//	// 填充数据包尾部
//	for (int m = 0; m < 32; m++) {
//		fa_data_pkg[eject_row * 32 + m] = fa_data_pkg[eject_row * 32 + m];
//	}
//	for (int m = 0; m < 32; m++) {
//		fa_data_pkg[eject_row * 32 + 32 + m] = fa_data_pkg[eject_row * 32 + m];
//	}
//
//
//	// 发送数据包
//	int result = sendto(udpSend, reinterpret_cast<const char*>(fa_data_pkg.data()), fa_data_pkg.size(), 0,
//		reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
//
//
//
//	// 关闭套接字
//	//closesocket(udpSend);
//	//WSACleanup();
//}