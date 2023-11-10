#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include "libmqtt.h"

static MqttBroker broker;
static pthread_t thread;
static int run;

// 下面两个结构用于唤醒另一个线程
static CRITICAL_SECTION criticalSection;
static CONDITION_VARIABLE conditionVar;

static const char* const szSend[] = {
    "publish qos = 0",
    "publish qos = 1",
    "publish qos = 2"
};
static const char* const szMqttRet[] = {
    "successful",
    "protocol version error",
    "ID error",
    "server error",
    "password error",
    "permission denied",
    "param error",
    "memory is not enough",
    "socket error",
    "no response"
};

// mqtt 接收数据并进行必要响应
void* recvPacket(void *param)
{
    while(run)
    {
        if(mqttThread(&broker) <= 0)
            run = 0;
    }
    return NULL;
}

// Ctrl+C 处理
void term(int sig)
{
    run = 0;
    printf("Goodbye!\n");
    mqttDisconnect(&broker);
    closesocket((SOCKET)broker.socket);
    WSACleanup();
    exit(0);
}

// mqtt 收到推送的回调
void recvCB(char *topic, char *msg, uint32_t msgLen)
{
    printf("\033[1;32m\"%s\" 发来 %dB\n%s\033[0m\n", topic, msgLen, msg);
}

int main(int argc, char** argv)
{
    WSADATA wsaData;
    SOCKET tcpc;
    struct sockaddr_in serverAddr;

    InitializeCriticalSection(&criticalSection);
    InitializeConditionVariable(&conditionVar);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("39.105.52.180");
    serverAddr.sin_port = htons(1883);

    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        WSACleanup();
        return -1;
    }
    tcpc = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_SOCKET == tcpc)
    {
        printf("create TCP socket error(%d)\n", WSAGetLastError());
        WSACleanup();
        return -2;
    }
    if(connect(tcpc, (SOCKADDR*)&serverAddr, sizeof(serverAddr)))
    {
        printf("connect TCP socket error(%d)\n", WSAGetLastError());
        closesocket(tcpc);
        WSACleanup();
        return -3;
    }

    broker.clientid = "clientid";
    broker.username = "username";
    broker.password = "password";
    broker.alive = 30;
    broker.seq = 1; // 消息 ID
    broker.seq2 = 0;
    broker.cleanSession = 1;
    broker.socket = (void*)tcpc;
    broker.recvCB = recvCB;
    broker.conditionVar = &conditionVar;
    broker.criticalSection = &criticalSection;

    run = 1;
    signal(SIGINT, term);
    pthread_create(&thread, NULL, recvPacket, NULL);

    printf("mqtt connect %s\n", szMqttRet[mqttConnect(&broker)]);
    printf("mqtt subscrib %s\n", szMqttRet[mqttSubscribe(&broker, "test/topic", 0)]);
    printf("mqtt unsubscrib %s\n", szMqttRet[mqttUnsubscribe(&broker, "test/topic")]);
    printf("mqtt subscrib %s\n", szMqttRet[mqttSubscribe(&broker, "test/topic", 2)]);

    printf("mqtt publish0 %s\n", szMqttRet[mqttPublish(&broker, "tp/aa", szSend[0], 0, 0)]);
    printf("mqtt publish1 %s\n", szMqttRet[mqttPublish(&broker, "tp/aa", szSend[1], 0, 1)]);
    printf("mqtt publish2 %s\n", szMqttRet[mqttPublish(&broker, "tp/aa", szSend[2], 0, 2)]);

    while(run)
    {
        Sleep(broker.alive * 1000);
        printf("Timeout! Sending ping...\n");
        mqttPing(&broker);
    }
    return 0;
}