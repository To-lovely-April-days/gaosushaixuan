// pch.h: 这是预编译标头文件。
// 下方列出的文件仅编译一次，提高了将来生成的生成性能。
// 这还将影响 IntelliSense 性能，包括代码完成和许多代码浏览功能。
// 但是，如果此处列出的文件中的任何一个在生成之间有更新，它们全部都将被重新编译。
// 请勿在此处添加要频繁更新的文件，这将使得性能优势无效。

#pragma once
#include <string>
#include <vector>
#include <cassert>

using namespace std;

#ifdef __linux__
  #if defined(BASECAMERA_CLASS_EXPORT)
    #define BASECAMERA_CLASS __attribute__((visibility("default")))
  #else 
    #define BASECAMERA_CLASS 
  #endif 
#else 
  // 保留原 Windows 定义 
  #if defined(BASECAMERA_CLASS_EXPORT)
    #define BASECAMERA_CLASS __declspec(dllexport)
  #else 
    #define BASECAMERA_CLASS __declspec(dllimport)
  #endif 
#endif 
#ifdef __linux__
#if defined(BASECAMERA_FACTORY_DLL_EXPORT)
#define BASECAMERA_FACTORY_DLL __attribute__((visibility("default")))
#else
#define BASECAMERA_FACTORY_DLL
#endif
#else
// 保留原 Windows 定义
#if defined(BASECAMERA_FACTORY_DLL_EXPORT)
#define BASECAMERA_FACTORY_DLL __declspec(dllexport)
#else
#define BASECAMERA_FACTORY_DLL __declspec(dllimport)
#endif
#endif
