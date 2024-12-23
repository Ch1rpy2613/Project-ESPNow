# Project-ESPNow
未命名，可称为无线小纸条，无线同步触摸或者其他任意二创/开发名称
此为开源项目，适用于常见的ESP32-CYD总成主板。Kurio Reiko感激一切对于本项目的支持，包括语言鼓励、项目转载、代码修缮或者产品再分发以及互补产品研发。商业化销售是被允许的，但是如果有人在未经声明原创作者而直接照搬代码进行商业化销售，那么他/她/它/They他妈的就是个傻逼。


![f32e759b6bf46be3565b28ebbf2d13e](https://github.com/user-attachments/assets/9870ed31-667e-4ff3-ab37-1c473c22b1a5)


现在提供简明烧录指北，需要的材料有：电脑、板子、数据线、手和眼睛（以Windows为例，我相信会用Linux的哥们不需要我这样的指北）

## 万事开头
不难，如图安装CH340驱动。无论哪种方式这都是必要的

![d4d80c0dc81a3c4baa0582a49186b62](https://github.com/user-attachments/assets/46b36394-1398-42ad-bf2b-eab2394cf620)

以下提供两种烧录方法：ino文件烧录和bin文件烧录。bin方式最简单。

## ino文件烧录
这是原教旨主义的编译和烧录，对ino文件有效，你可分析其代码结构逻辑，以及二创和调试

### 1.下载软件和库
https://www.arduino.cc/en/software，去官网把Arduino IDE下载好，选择你的版本

![8530af4605c7a8154c7da3b1ee96273](https://github.com/user-attachments/assets/0cfb522a-4bcd-4fe0-ae9b-543b141c9642)

软件安装好后请安装库，开发板管理器中安装ESP32库、其他库中安装TFT_eSPl和XPT2046_Touchscreen如下图所示

![0de05df178e2b08b54926ae1c88cdae](https://github.com/user-attachments/assets/90418fd9-bcc1-4446-a9f6-36c669638444)
![440da2c0a98be0fd6a5adc249efe432](https://github.com/user-attachments/assets/6398bf2c-6574-4ca7-817d-8f34544c15ce)
![6537afb5af77f23defc3eefc73c458d](https://github.com/user-attachments/assets/acfadf4d-358b-4e83-a0da-104a2d96e4e2)

#### 请注意，ESP32库耗时漫长且不一定能一次下载成功，故建议你提前一两天安装

### 2.修改TFT的setup.h
详见主页User_Setup.h和其相关介绍

### 3.配置端口和版型

现在，右键你的”此电脑“，打开”管理“，找到”设备管理器“。现在把你的板子连上电脑并在”端口“处找到你的设备

![0c4a891045e8e631deafaea6ec9a83d](https://github.com/user-attachments/assets/3b208fe4-e07d-4867-b74a-26949bc55ed0)
以我为例，我的板子是COM26端口（请不要用分线器/扩展坞）

然后回到你的Arduino IDE软件界面，顶部 工具-开发板=esp32 中找到”ESP32 Dev Module“或者”ESP32 WROOM DA Module“选中，
然后在这里选择你的端口

![54eb715eb5ece521ecd3436cc8143f6](https://github.com/user-attachments/assets/5e0fd326-44cc-4df1-908c-28d41dca3d0f)

到这里就结束了。一切良好的情况下，倘若你的库安装完全，和我的板子引脚也完全相同，那么点击中间的箭头，然后静候烧录完成即可。


## bin文件烧录
俗话说，Facker VS Bin，这和我们现在干的事情一点关系都没有。bin文件烧录适合在懒得研究Arduino IDE的情况下直接快速烧录出成品。但是bin文件不具备学习和代码调试相关的一切功能。但是他快，适合量产或者小白更新软件。

### 1.下载软件
现在马上立刻，去把首页flash_download_tool_3.9.3.zip下载下来然后解压，打开flash_download_tool_3.9.3.exe

### 2.选择版本
如图

![9977090efb2aa20c84130d79df0dfe7](https://github.com/user-attachments/assets/189b8aaa-ca71-48c6-a1c4-934842593171)

### 3.烧录

对于已编译的二进制bin文件，你通常会得到一堆bin文件，你只需要xxx.ino.bin 以及 xxx.ino.bootloader.bin 以及 xxx.ino.partitions.bin
其顺序和烧录地址如下：
![6bbc1be7cedc9fd12f9a844735493b5](https://github.com/user-attachments/assets/88259832-018c-4c3b-8c12-e20f420bb7ed)

右下角的COM为你的端口，请自行选择你的端口。

总之记住：bootloader.bin在前，partitions.bin在后，ino.bin最后。

ps：如果可以，你可以尝试把SPI SPEED跳到80MHz，把SPI MODE调成QIO。如果烧录不进去或者出毛病就改回来。如果你的bin文件只有一个，那么它的地址是0.



## 总之非常感谢你能读到这里，如果有兴趣，可以移步至我的个人b站账号https://space.bilibili.com/341918869?spm_id_from=333.1296.0.0 以及相关吹水群391437320
