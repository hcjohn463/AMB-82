#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNObjectDetection.h"
#include "VideoStreamOverlay.h"

// 硬件警报配置
#define LED_PIN 0    // LED 引脚
#define BUZZER_PIN 1 // 蜂鸣器引脚

// 视频流与模型配置
#define CHANNEL   0
#define CHANNELNN 3
#define NNWIDTH 640
#define NNHEIGHT 640

VideoSetting config(VIDEO_FHD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNObjectDetection objDet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

IPAddress ip;

// 警报状态变量
bool alarmStatus = false;


/*
 This sketch shows how to use tone api to play melody.

 Example guide:
 https://www.amebaiot.com/en/amebapro2-arduino-pwm-music/
 */

#define NOTE_    0
#define NOTE_B0  31
#define NOTE_C1  33
#define NOTE_CS1 35
#define NOTE_D1  37
#define NOTE_DS1 39
#define NOTE_E1  41
#define NOTE_F1  44
#define NOTE_FS1 46
#define NOTE_G1  49
#define NOTE_GS1 52
#define NOTE_A1  55
#define NOTE_AS1 58
#define NOTE_B1  62
#define NOTE_C2  65
#define NOTE_CS2 69
#define NOTE_D2  73
#define NOTE_DS2 78
#define NOTE_E2  82
#define NOTE_F2  87
#define NOTE_FS2 93
#define NOTE_G2  98
#define NOTE_GS2 104
#define NOTE_A2  110
#define NOTE_AS2 117
#define NOTE_B2  123
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951
#define NOTE_C8  4186
#define NOTE_CS8 4435
#define NOTE_D8  4699
#define NOTE_DS8 4978

int melody[] = {
    NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4, // 小蜜蜂前半部分
    NOTE_D4, NOTE_D4, NOTE_D4, NOTE_E4, NOTE_G4, NOTE_G4,          // 继续旋律
    NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4, // 后半部分
    NOTE_E4, NOTE_D4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_C4           // 结束
};

int noteDurations[] = {
    8, 8, 8, 8, 8, 8, 4, // 节奏对应小蜜蜂前半部分3
    8, 8, 4, 8, 8, 4,    // 继续节奏
    8, 8, 8, 8, 8, 8, 4, // 后半部分节奏
    8, 8, 8, 8, 8, 4     // 结束
};

int note = 0;
void play(int *melody, int *noteDurations, int num)
{
    int noteDuration = 3000 / noteDurations[note];

    tone(BUZZER_PIN, melody[note], noteDuration);

    delay(noteDuration * 1.0);

    if(note == num-1){
      note = 0;
    }
    else{
      note++;
    }
}
void handleAlarm(bool status) {
    if (status) {
        digitalWrite(LED_PIN, HIGH);
        play(melody, noteDurations, (sizeof(melody) / sizeof(int))); // 启动蜂鸣器
        // tone(BUZZER_PIN, 1000); // 启动蜂鸣器
    }else {
        digitalWrite(LED_PIN, LOW);
        noTone(BUZZER_PIN); // 关闭蜂鸣器
        note = 0;
    }
}

void setup() {
    Serial.begin(115200);

    // 初始化 LED 和蜂鸣器
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // 配置摄像头与视频流
    config.setBitrate(2 * 1024 * 1024); // 设置比特率
    Camera.configVideoChannel(CHANNEL, config);
    Camera.configVideoChannel(CHANNELNN, configNN);
    Camera.videoInit();

    // 配置 RTSP 服务
    rtsp.configVideo(config);
    rtsp.begin();

    // 连接到Wi-Fi
    char* ssid = "子晏iphone喔";
    char* password = "matt9015";
    WiFi.begin(ssid, password);

    // 配置目标检测模型
    objDet.configVideo(configNN);
    objDet.modelSelect(OBJECT_DETECTION, CUSTOMIZED_YOLOV7TINY, NA_MODEL, NA_MODEL);
    objDet.begin();

    // 配置视频流
    videoStreamer.registerInput(Camera.getStream(CHANNEL));
    videoStreamer.registerOutput(rtsp);
    videoStreamer.begin();
    Camera.channelBegin(CHANNEL);

    videoStreamerNN.registerInput(Camera.getStream(CHANNELNN));
    videoStreamerNN.registerOutput(objDet);
    videoStreamerNN.begin();
    Camera.channelBegin(CHANNELNN);

    OSD.configVideo(CHANNEL, config);
    OSD.begin();
}

const float confidenceThreshold = 50;

void loop() {
    // 处理串口输入
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim(); // 去除首尾空白字符

        Serial.print("Received command: ");
        Serial.println(input);

        // 解析命令
        if (input == "ALARM_ON") {
            alarmStatus = true;
            Serial.println("Alarm turned ON");
        } else if (input == "ALARM_OFF") {
            alarmStatus = false;
            Serial.println("Alarm turned OFF");
        } else if (input == "ALARM_TOGGLE") {
            alarmStatus = !alarmStatus;
            Serial.print("Alarm toggled to ");
            Serial.println(alarmStatus ? "ON" : "OFF");
        } else {
            Serial.println("Unknown command");
        }
    }

    // 根据警报状态控制 LED 和蜂鸣器
    handleAlarm(alarmStatus);

    // 处理对象检测并发送检测信息
    std::vector<ObjectDetectionResult> results = objDet.getResult();
    uint16_t im_h = config.height();
    uint16_t im_w = config.width();

    OSD.createBitmap(CHANNEL);

    if (objDet.getResultCount() > 0) {
        for (int i = 0; i < objDet.getResultCount(); i++) {
            ObjectDetectionResult item = results[i];
            int xmin = (int)(item.xMin() * im_w);
            int xmax = (int)(item.xMax() * im_w);
            int ymin = (int)(item.yMin() * im_h);
            int ymax = (int)(item.yMax() * im_h);

            String detectedClass = "unknown"; // 默认值
            int classIndex = results[i].type();
            if (classIndex == 0) {
                detectedClass = "close";
            } else if (classIndex == 1) {
                detectedClass = "open";
            }

            float confidence = item.score(); // 置信度

            if (confidence < confidenceThreshold){
              continue;
            }

            // 设置框的颜色，根据 detectedClass 值决定
            int boxColor = (detectedClass == "open") ? OSD_COLOR_GREEN : OSD_COLOR_RED;

            // 输出 JSON 格式数据
            Serial.print("{\"RTSP\": \"rtsp://");
            Serial.print(WiFi.localIP());
            Serial.print(":");
            Serial.print(rtsp.getPort());
            Serial.print("\", \"Detected\": \"");
            Serial.print(detectedClass);
            Serial.print("\", \"Confidence\": ");
            Serial.print(confidence, 2);
            Serial.println("}");

            // 绘制边框
            OSD.drawRect(CHANNEL, xmin, ymin, xmax, ymax, 3, boxColor);
            char text_str[50];
            snprintf(text_str, sizeof(text_str), "%s %.2f", detectedClass.c_str(), confidence);
            OSD.drawText(CHANNEL, xmin, ymin - OSD.getTextHeight(CHANNEL), text_str, OSD_COLOR_CYAN);
        }
    }

    OSD.update(CHANNEL);
    delay(100); // 延迟等待新结果
}
