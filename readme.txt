mqtt 收发数据

协议版本 3.1.1

todo:
1. send 的线程安全问题
2. 需验证 mqttThread 中对 qos = 2 的消息去重处理
3. mqttConnect 中未实现遗嘱功能
4. 到底要不要广泛使用 stdint.h ? 我认为应该只在必要情况下使用(对数据宽度有严格要求的情况下)
   但是函数的参数或返回值一旦使用到 stdint.h 中的类型, 就会引发连锁限制...

参考:
[1] https://github.com/mcxiaoke/mqtt
[2] https://github.com/fcvarela/liblwmqtt
