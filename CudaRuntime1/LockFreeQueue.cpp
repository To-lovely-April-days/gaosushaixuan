// ============================================================================
// 文件名：LockFreeQueue.cpp
// 作用  ：LockFreeQueue 模板类的显式实例化
// 说明  ：模板类的实现写在 .h 中（编译器需要看到完整定义）
//         本文件作用是强制实例化 LockFreeQueue<GrabData*>，方便链接器找到符号
// ============================================================================
#include "LockFreeQueue.h"
#include <CameraEnumerate.h>

// 显式实例化：让编译器为 LockFreeQueue<GrabData*> 生成所有方法的代码
template class LockFreeQueue<GrabData*>;
