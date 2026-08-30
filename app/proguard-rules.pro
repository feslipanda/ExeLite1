# Add project specific ProGuard rules here.
# You can control the set of applied configuration files using the
# proguardFiles setting in build.gradle.kts.

# ExeLite JNI fonksiyonlarını koru (R8 ile obfuscation yapılmasın)
-keep class com.exelite.engine.EngineJNI { *; }
-keep class com.exelite.engine.EngineManager { *; }
-keep class com.exelite.engine.EngineState { *; }
-keep class com.exelite.engine.EngineError { *; }
-keep class com.exelite.engine.AxisCode { *; }
-keep class com.exelite.engine.GamepadButton { *; }

# Winlator native (JNI) sınıflarını koru
-keep class com.winlator.xconnector.XConnectorEpoll { *; }
-keep class com.winlator.sysvshm.SysVSharedMemory { *; }

# Native method'ları koru
-keepclasseswithmembernames class * {
    native <methods>;
}
