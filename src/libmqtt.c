#include <string.h>
#include <stdlib.h>
#include "libmqtt.h"

// 以下 4 个函数是平台相关的底层 I/O 接口
extern int32_t mqttSend(void *socket, const void *data, unsigned int len);
extern int32_t mqttRecv(void *socket, void *data, unsigned int len);
extern int mqttWaitAck(MqttBroker *broker, unsigned int time);
extern void mqttWakeUp(MqttBroker *broker);

#define MQTT_DUP_FLAG       (1 << 3)
#define MQTT_QOS0_FLAG      (0 << 1)
#define MQTT_QOS1_FLAG      (1 << 1)
#define MQTT_QOS2_FLAG      (2 << 1)
#define MQTT_RETAIN_FLAG    (1 << 0)
#define MQTT_cleanSession   (1 << 1)
#define MQTT_WILL_FLAG      (1 << 2)
#define MQTT_willRetain     (1 << 5)
#define MQTT_USERNAME_FLAG  (1 << 7)
#define MQTT_PASSWORD_FLAG  (1 << 6)

/**
 * @brief   解析数据包 长度字段中 剩余的字节数
 * @param   buf [in] 指向数据包的指针
 * @return  MQTT 固定包头长度字段中剩余的字节数, 1 ~ 4 取决于消息的长度
 */
static uint8_t sizeofLenth(const uint8_t *buf)
{
    uint8_t len;

    for(len = 1; (len < 4) && (buf[len] & 0x80); len++);
    return len;
}

/**
 * @brief   解析数据包的剩余长度
 * @param   buf [in] 指向数据包的指针
 * @return  剩余长度
 */
static int32_t remainLenth(const uint8_t *buf)
{
    uint16_t multiplier = 1;
    int32_t value = 0;
    uint8_t digit;

    buf++; // 跳过固定头中的 "flags" 字段 (1 字节)
    do {
        digit = *buf;
        value += (digit & 127) * multiplier;
        multiplier *= 128;
        buf++;
    } while(digit & 0x80);
    return value;
}

/**
 * @brief   创建报文
 * @param   pBuf [out] 指向数据包指针的指针
 * @param   type [in] 报文类型和标志
 * @param   remain [in] 剩余长度
 * @param   save [in] 分配内存时减少分配的长度, 用于节省内存,
 *                    如 publish 时已经有内存存储 message 了, 可以分段发送数据
 * @return  创建成功返回报文总长度 - save, 即分配的内存长度, 否则返回 0
 * @warning 成功创建报文后必须释放 *pBuf
 */
static int32_t packetCreate(uint8_t **pBuf, uint8_t type, int32_t remain, int32_t save)
{
    int32_t packetLen;
    int32_t i;

    packetLen = 1; // 固定头第 1 个字节
    i = remain;
    do
    {
        packetLen++;
        i >>= 7;
    } while(i);
    packetLen += remain; // 报文总长度
    *pBuf = malloc(packetLen - save);
    if(*pBuf)
    {
        (*pBuf)[i++] = type; // 填写报文类型和标志
        do
        {
            (*pBuf)[i] = remain & 0x7F; // 填写剩余长度
            remain >>= 7;
            if(remain > 0)
                (*pBuf)[i] = (*pBuf)[i] | 128;
            i++;
        } while(remain > 0);
        return packetLen - save;
    }
    else
        return 0;
}

/**
 * @brief   向报文写入带长度信息的负载数据
 * @param   buf [out] 指向数据包的指针
 * @param   offset [in/out] 数据偏移量
 * @param   data [in] 要写入的数据
 * @param   lenth [in] 数据长度
 */
static void packetWrite(uint8_t *buf, int32_t *offset, const void *data, uint16_t lenth)
{
    buf[(*offset)++] = lenth >> 8;
    buf[(*offset)++] = lenth & 0xFF;
    memcpy(buf + (*offset), data, lenth);
    (*offset) += lenth;
}

uint16_t mqttMsgID(const uint8_t *buf)
{
    uint16_t id = 0;
    uint16_t offset;
    uint8_t type = MQTTParseMessageType(buf);
    uint8_t qos = MQTTParseMessageQos(buf);
    uint8_t rlb;

    if(type >= MQTT_MSG_PUBLISH && type <= MQTT_MSG_UNSUBACK)
    {
        if(type == MQTT_MSG_PUBLISH)
        {
            if(qos != 0)
            {
                // 固定头长度 + topic (UTF 编码)
                // flags 1 字节 + rlb 表示长度的字节数 + topic 大小
                rlb = sizeofLenth(buf);
                offset = *(buf + 1 + rlb) << 8;   // topic UTF MSB
                offset |= *(buf + 1 + rlb + 1);   // topic UTF LSB
                offset += (1 + rlb + 2);          // 固定头 + topic 长度
                id = *(buf + offset) << 8;        // ID MSB
                id |= *(buf + offset + 1);        // ID LSB
            }
        }
        else
        {
            // 固定头长度
            // flags 1 字节 + rlb 表示长度的字节数
            rlb = sizeofLenth(buf);
            id = *(buf + 1 + rlb) << 8;    // ID MSB
            id |= *(buf + 1 + rlb + 1);    // ID LSB
        }
    }
    return id;
}

uint16_t mqttGetTopic(const uint8_t *buf, const uint8_t **ppTopic)
{
    uint16_t topicLen;
    uint8_t rlb;

    if(MQTTParseMessageType(buf) == MQTT_MSG_PUBLISH)
    {
        // 固定标头长度 = [1("flags"字节) + rlb (表示长度字节)]
        rlb = sizeofLenth(buf);
        topicLen = *(buf + 1 + rlb) << 8;    // MSB of topic UTF
        topicLen |= *(buf + 1 + rlb + 1);    // LSB of topic UTF
        // topic 起始地址 = [1("flags"字节) + rlb (表示长度字节) + 2 (表示 UTF)]
        *ppTopic = (buf + (1 + rlb + 2));
    }
    else
    {
        *ppTopic = NULL;
        topicLen = 0;
    }
    return topicLen;
}

int32_t mqttGetMsg(const uint8_t *buf, const uint8_t **ppMsg)
{
    int32_t msglen;
    uint8_t rlb;
    uint8_t offset;

    if(MQTTParseMessageType(buf) == MQTT_MSG_PUBLISH)
    {
        // 消息以 [固定头长度 + topic(UTF 编码) + 消息 ID(如果 QoS > 0)] 开始
        rlb = sizeofLenth(buf);
        offset = (*(buf + 1 + rlb)) << 8; // topic UTF MSB
        offset |= *(buf + 1 + rlb + 1);   // topic UTF LSB
        offset += (1 + rlb + 2);          // 固定头长度 + topic 大小
        if(MQTTParseMessageQos(buf))
            offset += 2;                  // 加 ID 2 字节
        *ppMsg = (buf + offset);

        // offset 现在指向消息的开头, 消息的长度是剩余长度
        // 可变头是偏移量 - 固定头 1 + rlb
        // 因此 lom = remlen - (offset - (1 + rlb))
        msglen = remainLenth(buf) - (offset - (rlb + 1));
    }
    else
    {
        *ppMsg = NULL;
        msglen = 0;
    }
    return msglen;
}

MqttRet mqttConnect(MqttBroker *broker)
{
    uint8_t *packet;
    uint16_t clientidlen = strlen(broker->clientid);
    uint16_t usernamelen = strlen(broker->username);
    uint16_t passwordlen = strlen(broker->password);
    uint16_t packetlen;
    uint16_t remainLen;
    int32_t offset;
    MqttRet ret;

    // 可变头
    remainLen = 10;
    // 负载 ID
    if(clientidlen)
        remainLen += 2 + clientidlen;
    else
        return MQTT_PARAM_ERR;
    // 负载 username
    if(usernamelen)
        remainLen += 2 + usernamelen;
    // 负载 password
    if(passwordlen)
        remainLen += 2 + passwordlen;
    packetlen = packetCreate(&packet, MQTT_MSG_CONNECT, remainLen, 0);
    if(!packet)
        return MQTT_MEM_ERR;
    offset = sizeofLenth(packet) + 1;
    packetWrite(packet, &offset, "MQTT", 4);
    packet[offset++] = 0x04; // 协议版本 3.1.1
    // 连接标志字节
    if(usernamelen)
        packet[offset] |= MQTT_USERNAME_FLAG;
    if(passwordlen)
        packet[offset] |= MQTT_PASSWORD_FLAG;
    if(broker->cleanSession)
        packet[offset] |= MQTT_cleanSession;
    offset++;
    packet[offset++] = broker->alive >> 8;   // Keep alive MSB
    packet[offset++] = broker->alive & 0xFF; // Keep alive LSB
    // Client ID - UTF 编码
    packetWrite(packet, &offset, broker->clientid, clientidlen);
    if(usernamelen)
        packetWrite(packet, &offset, broker->username, usernamelen);
    if(passwordlen)
        packetWrite(packet, &offset, broker->password, passwordlen);
    ret = MQTT_OK;
    // 等待回复 (offset 用于计数)
    broker->waitType = MQTT_MSG_CONNACK;
    for(offset = 0; offset < MQTT_RETRY; offset++)
    {
        if(mqttSend(broker->socket, packet, packetlen) < packetlen)
        {
            ret = MQTT_SEND_ERR; // 一旦发送出错, 立刻终止重传
            break;
        }
        if(mqttWaitAck(broker, MQTT_TIMEOUE))
            break; // 收到期望的回复则返回, 超时未收到期望的回复则重传
    }
    free(packet);
    if(MQTT_OK == ret)
    {
        if(MQTT_RETRY == offset)
            return MQTT_ACK_ERR; // 服务器不理我
        else
            return broker->waitParam; // 应该是 <= 5 的数值, 描述连接返回码
    }
    else
        return ret; // 一定是 MQTT_SEND_ERR
}

MqttRet mqttDisconnect(MqttBroker *broker)
{
    static const uint8_t packet[] = {
        MQTT_MSG_DISCONNECT, // 消息类型, DUP 标志, QoS 级别, Retain
        0x00 // 剩余长度
    };

    if(mqttSend(broker->socket, packet, sizeof(packet)) < (int32_t)sizeof(packet))
        return MQTT_SEND_ERR;
    return MQTT_OK;
}

MqttRet mqttPing(MqttBroker *broker)
{
    static const uint8_t packet[] = {
        MQTT_MSG_PINGREQ, // 消息类型, DUP 标志, QoS 级别, Retain
        0x00 // 剩余长度
    };

    if(mqttSend(broker->socket, packet, sizeof(packet)) < (int)sizeof(packet))
        return MQTT_SEND_ERR;
    return MQTT_OK;
}

MqttRet mqttPublish(MqttBroker *broker, const char *topic, const char *msg, uint8_t retain, uint8_t qos)
{
    uint8_t *packet;
    uint16_t packetlen;
    uint16_t topiclen = strlen(topic);
    uint16_t msglen = strlen(msg);
    int32_t offset;
    MqttRet ret;

    packetlen = packetCreate(&packet, MQTT_MSG_PUBLISH | ((qos & 0x03) << 1) | (!!retain), \
                             topiclen + 2 + (qos ? 2 : 0) + msglen, msglen);
    if(!packet)
        return MQTT_MEM_ERR;
    offset = sizeofLenth(packet) + 1;
    packetWrite(packet, &offset, topic, topiclen);
    if(qos)
    {
        packet[offset++] = broker->seq >> 8;
        packet[offset++] = broker->seq & 0xFF;
    }
    ret = MQTT_OK;
    // 等待回复 (offset 用于计数)
    if(1 == qos)
        broker->waitType = MQTT_MSG_PUBACK;
    if(2 == qos)
        broker->waitType = MQTT_MSG_PUBREC;
    broker->waitParam = broker->seq;
    for(offset = 0; offset < MQTT_RETRY; offset++)
    {
        // 分两段发送报文, 减少内存占用
        if(mqttSend(broker->socket, packet, packetlen) < packetlen)
        {
            ret = MQTT_SEND_ERR;
            break;
        }
        if(mqttSend(broker->socket, msg, msglen) < msglen)
        {
            ret = MQTT_SEND_ERR;
            break;
        }
        if(qos)
        {
            if(mqttWaitAck(broker, MQTT_TIMEOUE))
            {
                if(2 == qos)
                {
                    broker->seq++;
                    if(!broker->seq)
                        broker->seq++;
                    broker->waitType = MQTT_MSG_PUBCOMP;
                    broker->waitParam = broker->seq;
                    for(offset = 0; offset < MQTT_RETRY; offset++)
                    {
                        if(mqttPubRetuen(broker, MQTT_MSG_PUBREL, broker->seq))
                        {
                            ret = MQTT_SEND_ERR;
                            break;
                        }
                        if(mqttWaitAck(broker, MQTT_TIMEOUE))
                            break; // 收到期望的回复则返回, 超时未收到期望的回复则重传
                    }
                }
                break; // 收到期望的回复则返回, 超时未收到期望的回复则重传
            }
        }
        else
            break; // QOS = 0 时只发送一次
    }
    free(packet);
    if(qos)
    {
        broker->seq++;
        if(!broker->seq)
            broker->seq++;
    }
    if(MQTT_OK == ret && MQTT_RETRY == offset)
        return MQTT_ACK_ERR; // 服务器不理我
    else
        return ret;
}

MqttRet mqttPubRetuen(MqttBroker *broker, uint8_t type, uint16_t msgID)
{
    uint8_t packet[] = {
        type | MQTT_QOS1_FLAG,
        0x02, // 剩余长度
        msgID >> 8,
        msgID & 0xFF
    };

    if(mqttSend(broker->socket, packet, sizeof(packet)) < (int32_t)sizeof(packet))
        return MQTT_SEND_ERR;
    return MQTT_OK;
}

MqttRet mqttSubscribe(MqttBroker *broker, const char *topic, uint8_t qos)
{
    uint8_t *packet;
    uint16_t topiclen;
    int32_t packetlen;
    int32_t offset;
    MqttRet ret;

    topiclen = strlen(topic);
    packetlen = packetCreate(&packet, MQTT_MSG_SUBSCRIBE | MQTT_QOS1_FLAG, topiclen + 5, 0);
    if(!packet)
        return MQTT_MEM_ERR;
    offset = sizeofLenth(packet) + 1;
    // 可变头
    packet[offset++] = broker->seq >> 8; // Message ID
    packet[offset++] = broker->seq & 0xFF;
    packetWrite(packet, &offset, topic, topiclen);
    packet[offset] = qos;
    ret = MQTT_OK;
    // 等待回复 (offset 用于计数)
    broker->waitType = MQTT_MSG_SUBACK;
    broker->waitParam = broker->seq;
    for(offset = 0; offset < MQTT_RETRY; offset++)
    {
        if(mqttSend(broker->socket, packet, packetlen) < packetlen)
        {
            ret = MQTT_SEND_ERR;
            break;
        }
        if(mqttWaitAck(broker, MQTT_TIMEOUE))
            break; // 收到期望的回复则返回, 超时未收到期望的回复则重传
    }
    free(packet);
    broker->seq++;
    if(!broker->seq)
        broker->seq++;
    if(MQTT_OK == ret && MQTT_RETRY == offset)
        return MQTT_ACK_ERR; // 服务器不理我
    else
        return ret;
}

MqttRet mqttUnsubscribe(MqttBroker *broker, const char *topic)
{
    uint8_t *packet;
    uint16_t topiclen;
    int32_t packetlen;
    int32_t offset;
    MqttRet ret;

    topiclen = strlen(topic);
    packetlen = packetCreate(&packet, MQTT_MSG_UNSUBSCRIBE | MQTT_QOS1_FLAG, topiclen + 4, 0);
    if(!packet)
        return MQTT_MEM_ERR;
    offset = sizeofLenth(packet) + 1;
    // 可变头
    packet[offset++] = broker->seq >> 8; // Message ID
    packet[offset++] = broker->seq & 0xFF;
    packetWrite(packet, &offset, topic, topiclen);
    ret = MQTT_OK;
    // 等待回复 (offset 用于计数)
    broker->waitType = MQTT_MSG_UNSUBACK;
    broker->waitParam = broker->seq;
    for(offset = 0; offset < MQTT_RETRY; offset++)
    {
        if(mqttSend(broker->socket, packet, packetlen) < packetlen)
        {
            ret = MQTT_SEND_ERR;
            break;
        }
        if(mqttWaitAck(broker, MQTT_TIMEOUE))
            break; // 收到期望的回复则返回, 超时未收到期望的回复则重传
    }
    free(packet);
    broker->seq++;
    if(!broker->seq)
        broker->seq++;
    if(MQTT_OK == ret && MQTT_RETRY == offset)
        return MQTT_ACK_ERR; // 服务器不理我
    else
        return ret;
}

/**
 * @brief   阻塞式接收报文, 将收到的数据包放到 broker->recvBuf 里
 * @param   broker [in] broker 指针
 * @return  >0 成功并返回报文长度, 0 连接已关闭, -1 IO 错误, -2 内存不足
 * @warning 使用完毕后需要释放 broker->recvBuf
 */
static int32_t mqttGetPacket(MqttBroker *broker)
{
    static uint8_t fixedHeader[5]; // 最大 5 字节
    int32_t lenth, total, packetlen;

    // 先收 2 字节固定头
    for(total = 0; total < 2; total += lenth)
    {
        if((lenth = mqttRecv(broker->socket, fixedHeader + total, 2 - total)) <= 0)
            return lenth;
    }
    // 接收完整的固定头
    for(; fixedHeader[total - 1] & 0x80; total += lenth)
    {
        if(total >= (int32_t)sizeof(fixedHeader))
            return -1;
        if((lenth = mqttRecv(broker->socket, (char*)fixedHeader + total, 1)) <= 0)
            return lenth;
    }

    packetlen = 1 + sizeofLenth(fixedHeader) + remainLenth(fixedHeader);
    broker->recvBuf = (uint8_t*)malloc(packetlen);
    if(!broker->recvBuf)
        return -2;
    memcpy(broker->recvBuf, fixedHeader, total);
    for(; total < packetlen; total += lenth)
    {
        if((lenth = mqttRecv(broker->socket, (char*)(broker->recvBuf) + total, packetlen - total)) <= 0)
            return lenth;
    }
    return total;
}

int mqttThread(MqttBroker *broker)
{
    int32_t len;
    char *topic, *msg;
    const uint8_t *pTopic, *pMsg;
    int ret;

    // 接收一个完整数据包
    ret = mqttGetPacket(broker);
    if(ret > 0)
    {
        // 如果收到了期望的消息就唤醒正在等待的线程
        // 期望的消息: 报文类型和 ID 都是想要的值; 但是 CONNACK 报文不返回 ID,
        // 而是服务器的响应, 所以 broker->waitType 设置成 MQTT_MSG_CONNACK 时 broker->waitParam 作为输出.
        if((broker->waitType == MQTTParseMessageType(broker->recvBuf) && broker->waitParam == mqttMsgID(broker->recvBuf)) \
           || (MQTT_MSG_CONNACK == broker->waitType))
        {
            broker->waitType = 0;
            // broker->waitType 为 MQTT_MSG_CONNACK 时输出服务器返回的响应
            if(MQTT_MSG_CONNACK == broker->waitType)
                broker->waitParam = broker->recvBuf[3];
            mqttWakeUp(broker);
        }
        // 收到推送
        if(MQTTParseMessageType(broker->recvBuf) == MQTT_MSG_PUBLISH)
        {
            len = mqttGetTopic(broker->recvBuf, &pTopic);
            topic = malloc(len + 1);
            if(topic)
            {
                memcpy(topic, pTopic, len);
                topic[len] = '\0'; // for printf
            }
            len = mqttGetMsg(broker->recvBuf, &pMsg);
            msg = malloc(len + 1);
            if(msg)
            {
                memcpy(msg, pMsg, len);
                msg[len] = '\0'; // for printf
            }
            // 收到推送, 调用回调函数 (过滤 qos = 2 时的重复消息)
            if(mqttMsgID(broker->recvBuf) != broker->seq2)
                broker->recvCB(topic, msg, len);
            // Qos 1 需要回复 PUBACK
            if(MQTTParseMessageQos(broker->recvBuf) == 1)
                mqttPubRetuen(broker, MQTT_MSG_PUBACK, mqttMsgID(broker->recvBuf));
            // Qos 2 第一步回复 PUBREC
            if(MQTTParseMessageQos(broker->recvBuf) == 2)
            {
                broker->seq2 = mqttMsgID(broker->recvBuf);
                mqttPubRetuen(broker, MQTT_MSG_PUBREC, mqttMsgID(broker->recvBuf));
            }
            if(topic)
                free(topic);
            if(msg)
                free(msg);
        }
        // Qos 2 第二步回复 PUBCOMP
        if(MQTTParseMessageType(broker->recvBuf) == MQTT_MSG_PUBREL)
        {
            broker->seq2 = 0;
            mqttPubRetuen(broker, MQTT_MSG_PUBCOMP, mqttMsgID(broker->recvBuf));
        }
        // 记得释放内存
        free(broker->recvBuf);
    }
    return ret;
}
