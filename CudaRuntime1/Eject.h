#pragma once

extern char firstByte;
void ControlValvesWithRows(char* tags, int img_width);
void EnhanceTags(char* tags_new, int rows, int cols, int numAdd, char* tags_Enhance);
void Start_send();
void UDP_receive_thread();
void send_pkg_256(char* tags_new);


