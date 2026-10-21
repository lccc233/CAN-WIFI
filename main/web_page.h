#pragma once
// 页面组装：raw literal 在编译期拼接为完整 HTML。
// - web_head.h：<!DOCTYPE>..<head>/<style>/<body>（含 7KB base64 水印）
// - web_body.h：页面 DOM 结构
// - web_js.h  ：前端脚本 <script>..</html>
// - web_battery.h：电池协议被动解码，在主脚本前加载定义
#include "web_head.h"
#include "web_body.h"
#include "web_js.h"
#include "web_battery.h"

#define INDEX_HTML PAGE_HEAD PAGE_BODY PAGE_BATTERY_JS PAGE_JS
