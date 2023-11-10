#include <windows.h>
#include <winsock.h>
#include "libmqtt.h"

int32_t mqttSend(void *socket, const void *data, unsigned int len)
{
    return send((SOCKET)socket, data, len, 0);
}

int32_t mqttRecv(void *socket, void *data, unsigned int len)
{
    return recv((SOCKET)socket, (char*)data, len, 0);
}

int mqttWaitAck(MqttBroker *broker, unsigned int time)
{
    return SleepConditionVariableCS((CONDITION_VARIABLE*)(broker->conditionVar), \
            (CRITICAL_SECTION*)(broker->criticalSection), time);
}

void mqttWakeUp(MqttBroker *broker)
{
    WakeConditionVariable((CONDITION_VARIABLE*)(broker->conditionVar));
}
