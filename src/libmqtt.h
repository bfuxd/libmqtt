#ifndef __LIBMQTT_H
#define __LIBMQTT_H

#include <stdint.h>

#define MQTT_MSG_CONNECT       (1 << 4)
#define MQTT_MSG_CONNACK       (2 << 4)
#define MQTT_MSG_PUBLISH       (3 << 4)
#define MQTT_MSG_PUBACK        (4 << 4)
#define MQTT_MSG_PUBREC        (5 << 4)
#define MQTT_MSG_PUBREL        (6 << 4)
#define MQTT_MSG_PUBCOMP       (7 << 4)
#define MQTT_MSG_SUBSCRIBE     (8 << 4)
#define MQTT_MSG_SUBACK        (9 << 4)
#define MQTT_MSG_UNSUBSCRIBE  (10 << 4)
#define MQTT_MSG_UNSUBACK     (11 << 4)
#define MQTT_MSG_PINGREQ      (12 << 4)
#define MQTT_MSG_PINGRESP     (13 << 4)
#define MQTT_MSG_DISCONNECT   (14 << 4)

// 报文重传时间间隔
#define MQTT_TIMEOUE           3000
// 报文重传最大次数
#define MQTT_RETRY             3

typedef struct
{
    void *socket;
    uint8_t *recvBuf;
    void (*recvCB)(char *topic, char *msg, uint32_t msgLen);
    const char *clientid;
    const char *username;
    const char *password;
    // const char *WillTopic;
    // uint8_t willRetain;
    // uint8_t willQos;
    uint8_t cleanSession;
    // Management fields
    uint16_t seq, seq2;
    uint16_t alive;
    uint8_t waitType;
    uint16_t waitParam;
    // 以下成员根据平台对条件变量的要求增减
    void *conditionVar;
    void *criticalSection;
} MqttBroker;

typedef enum
{
    MQTT_OK,               // 成功
    MQTT_VERSION_ERR,      // 连接已被拒绝, 不支持的协议版本
    MQTT_ID_ERR,           // 连接已被拒绝, 不合格的客户端标识符
    MQTT_SERVER_ERR,       // 连接已被拒绝, 服务端不可用
    MQTT_PASSWORD_ERR,     // 连接已被拒绝, 无效的用户名或密码
    MQTT_PERMISSION_ERR,   // 连接已被拒绝, 未授权
    MQTT_PARAM_ERR,        // 输入参数错误
    MQTT_MEM_ERR,          // 内存不足
    MQTT_SEND_ERR,         // socket 发送错误
    MQTT_ACK_ERR           // 服务器超时无响应
} MqttRet;

/**
 * @brief   从缓冲区中提取消息类型
 * @param   buf [in] 指向数据包的指针
 * @return  MQTT 消息类型字节
 */
#define MQTTParseMessageType(buffer) (*((uint8_t*)buffer) & 0xF0)

/**
 * @brief   指示它是否是重复的数据包
 * @param   buf [in] 指向数据包的指针
 * @return  0 不重复 !=0 重复
 */
#define MQTTParseMessageDuplicate(buffer) (*((uint8_t*)buffer) & 0x08)

/**
 * @brief   提取消息 QoS 级别
 * @param   buf [in] 指向数据包的指针
 * @return  QoS 级别 (0, 1 或 2)
 */
#define MQTTParseMessageQos(buffer) ((*((uint8_t*)buffer) & 0x06) >> 1)

/**
 * @brief   指示此数据包是否具有保留标志
 * @param   buf [in] 指向数据包的指针
 * @return  0 不包含 !=0 包含
 */
#define MQTTParseMessageRetain(buffer) (*((uint8_t*)buffer) & 0x01)

/**
 * @brief   解析数据包中的消息 ID
 * @param   buf [in] 指向数据包的指针
 * @return  消息 ID
 */
extern uint16_t mqttMsgID(const uint8_t *buf);

/**
 * @brief   解析数据包中的 topic
 * @param   buf [in] 指向数据包的指针
 * @param   ppTopic [out] 指向 topic 缓冲区的指针
 * @return  topic 大小 (以字节为单位) (0 = 缓冲区中没有发布消息)
 * @warning ppTopic 只输出了一个指向 buf 数据部分的指针, 修改 *ppTopic 指向的内容会破坏 buf
 */
extern uint16_t mqttGetTopic(const uint8_t *buf, const uint8_t **ppTopic);

/**
 * @brief   解析发布的数据包中消息内容
 * @param   buf [in] 指向数据包的指针
 * @param   ppMsg [out] 指向消息缓冲区的指针
 * @return  消息大小 (以字节为单位) (0 = 缓冲区中没有发布消息)
 * @warning ppMsg 只输出了一个指向 buf 数据部分的指针, 修改 *ppMsg 指向的内容会破坏 buf
 */
extern int32_t mqttGetMsg(const uint8_t *buf, const uint8_t **ppMsg);

/**
 * @brief   连接到 broker
 * @param   broker [in] broker 指针
 * @return  参考 MqttRet
 */
extern MqttRet mqttConnect(MqttBroker *broker);

/**
 * @brief   断开连接
 * @param   broker [in] broker 指针
 * @return  参考 MqttRet
 * @warning 随后需要关闭 socket 连接
 */
extern MqttRet mqttDisconnect(MqttBroker *broker);

/**
 * @brief   Ping 一下
 * @param   broker [in] broker 指针
 * @return  参考 MqttRet
 * @warning 随后需要关闭 socket 连接
 */
extern MqttRet mqttPing(MqttBroker *broker);

/**
 * @brief   向某个 topic 发布消息
 * @param   broker [in] broker 指针
 * @param   topic [in] topic 字符串
 * @param   msg [in] 消息内容
 * @param   retain [in] 是否启用 Retain 标志 (1 启用, 0 禁用)
 * @param   qos [in] (0, 1, 2)
 * @return  参考 MqttRet
 */
extern MqttRet mqttPublish(MqttBroker *broker, const char *topic, const char *msg, uint8_t retain, uint8_t qos);

/**
 * @brief   发送特定类型的 publish 响应包
 * @param   broker [in] broker 指针
 * @param   type [in] 响应类型
 * @return  参考 MqttRet
 */
extern MqttRet mqttPubRetuen(MqttBroker *broker, uint8_t type, uint16_t msgID);

/**
 * @brief   订阅某个 topic
 * @param   broker [in] broker 指针
 * @param   topic [in] topic 字符串
 * @return  参考 MqttRet
 */
extern MqttRet mqttSubscribe(MqttBroker *broker, const char *topic, uint8_t qos);

/**
 * @brief   取消订阅某个 topic
 * @param   broker [in] broker 指针
 * @param   topic [in] topic 字符串
 * @return  参考 MqttRet
 */
extern MqttRet mqttUnsubscribe(MqttBroker *broker, const char *topic);

/**
 * @brief   mqtt 报文接收与响应业务
 * @param   broker [in] broker 指针
 * @return  参考 MqttRet
 * @warning 创建 TCP 连接后应立即在新线程中循环调用
 */
extern int mqttThread(MqttBroker *broker);


#endif // __LIBMQTT_H
