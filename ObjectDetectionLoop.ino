#include "WiFi.h"
#include "StreamIO.h"
#include "VideoStream.h"
#include "RTSP.h"
#include "NNObjectDetection.h"
#include "VideoStreamOverlay.h"

// 硬件警报配置0
#define LED_PIN 0    // LED 引脚
#define BUZZER_PIN 1 // 蜂鸣器引脚

// 视频流与模型配置（保持不变）
#define CHANNEL   0
#define CHANNELNN 3
#define NNWIDTH 576
#define NNHEIGHT 320

VideoSetting config(VIDEO_FHD, 30, VIDEO_H264, 0);
VideoSetting configNN(NNWIDTH, NNHEIGHT, 10, VIDEO_RGB, 0);
NNObjectDetection objDet;
RTSP rtsp;
StreamIO videoStreamer(1, 1);
StreamIO videoStreamerNN(1, 1);

IPAddress ip;

// 警报状态变量
bool alarmStatus = false;
void handleAlarm(bool status) {
    if (status) {
        digitalWrite(LED_PIN, HIGH);
        tone(BUZZER_PIN, 1000); // 启动蜂鸣器
    } else {
        digitalWrite(LED_PIN, LOW);
        noTone(BUZZER_PIN); // 关闭蜂鸣器
    }
}
void setup() {
    Serial.begin(115200);

    // 初始化 LED 和蜂鸣器
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // 配置摄像头与视频流（保持不变）
    config.setBitrate(2 * 1024 * 1024); // 设置比特率
    Camera.configVideoChannel(CHANNEL, config);
    Camera.configVideoChannel(CHANNELNN, configNN);
    Camera.videoInit();

    // 配置 RTSP 服务
    rtsp.configVideo(config);
    rtsp.begin();

    // 连接到Wi-Fi
    char* ssid = "3H1F WIFI-2.4G";
    char* password = "54585839";
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

            // 打印检测结果到串口

            Serial.print("RTSP URL: rtsp://");
            Serial.print(WiFi.localIP());
            Serial.print(":");
            Serial.print(rtsp.getPort());
            Serial.print(", Detected: ");
            Serial.print(detectedClass);
            Serial.print(", Confidence: ");
            Serial.println(confidence);

            // 绘制边框
            OSD.drawRect(CHANNEL, xmin, ymin, xmax, ymax, 3, OSD_COLOR_GREEN);
            char text_str[50];
            snprintf(text_str, sizeof(text_str), "%s %.2f", detectedClass.c_str(), confidence);
            OSD.drawText(CHANNEL, xmin, ymin - OSD.getTextHeight(CHANNEL), text_str, OSD_COLOR_CYAN);
        }
    }

    OSD.update(CHANNEL);
    delay(100); // 延迟等待新结果
}