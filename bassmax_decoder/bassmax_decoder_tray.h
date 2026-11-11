// bassmax_decoder_tray.h — bassmax_decoder.ax 自带的进程内托盘
//
// 由 BmdFilter 构造/析构驱动引用计数: 播放时图标出现, 滤镜销毁时图标消失。
// 不注册 COM 类、不导出符号、不碰 ISpecifyPropertyPages —— 与属性页冲突无关。
#pragma once
namespace bassmax {

void DecoderTrayAddRef();     // 滤镜构造: 引用 +1, 首次调用起托盘线程
void DecoderTrayRelease();   // 滤镜析构: 引用 -1, 只发消息绝不阻塞拆图线程

} // namespace bassmax
