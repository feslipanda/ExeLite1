// ============================================================
// jni_utils.h / jni_utils.cpp — JNI Yardımcı Fonksiyonlar
// ============================================================
#pragma once
#include <jni.h>
#include <string>

namespace jni_utils {
    std::string jstring_to_str(JNIEnv* env, jstring js);
    jstring     str_to_jstring(JNIEnv* env, const std::string& s);
}
