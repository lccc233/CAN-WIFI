#pragma once

// 页头 <head> 到 <body>：含 base64 水印图 line 过长，单独成文件
#define PAGE_HEAD R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CAN Bus Monitor</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body {
    font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
    background: #f5f5f5; color: #333;
    display: flex; flex-direction: column;
    height: 100dvh; overflow: hidden;
    position: relative;
  }
  body::before {
    content: '';
    position: fixed;
    top: 50%; left: 50%;
    transform: translate(-50%, -50%);
    width: 50%; max-width: 350px;
    height: auto;
    aspect-ratio: 1;
    background: url('data:image/png;base64,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') center/contain no-repeat;
    opacity: 0.08;
    pointer-events: none;
    z-index: 0;
  }
  .header, .content { position: relative; z-index: 1; }
  .header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 8px 16px; background: #fff;
    border-bottom: 1px solid #ddd;
    box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  }
  .header h1 { font-size: 1rem; color: #1a73e8; }
  .header-right { display: flex; align-items: center; gap: 12px; }
  .status { display: flex; align-items: center; gap: 4px; font-size: 0.8rem; }
  .dot { width: 8px; height: 8px; border-radius: 50%; }
  .dot.on { background: #34a853; }
  .dot.off { background: #ea4335; }
  .btn {
    padding: 5px 14px; border: 1px solid #ccc; border-radius: 4px;
    background: #fff; color: #333; cursor: pointer; font-size: 0.8rem;
    font-family: inherit; transition: background 0.15s;
  }
  .btn:hover { background: #f0f0f0; }
  .btn.danger { border-color: #ea4335; color: #ea4335; }
  .btn.danger:hover { background: #fce8e6; }
  .btn.primary { border-color: #1a73e8; color: #fff; background: #1a73e8; }
  .btn.primary:hover { background: #1557b0; }
  .btn.back { border-color: #666; color: #666; }

  .content { flex: 1; overflow-y: auto; overflow-x: auto; padding-bottom: 72px; }
  .detail-header {
    padding: 8px 16px; background: #e8f0fe; border-bottom: 1px solid #c2d7f5;
    display: flex; align-items: center; gap: 12px; font-size: 0.85rem;
  }
  .detail-header .id-label { color: #1a73e8; font-weight: bold; font-size: 1rem; }

  /* 详情视图：Chart / Table 切换 */
  .tab {
    padding: 3px 12px; border: 1px solid #c2d7f5; border-radius: 4px;
    background: #fff; color: #1a73e8; cursor: pointer;
    font-size: 0.75rem; font-family: inherit;
  }
  .tab.active { background: #1a73e8; color: #fff; border-color: #1a73e8; }

  table { width: 100%; border-collapse: collapse; font-size: 0.82rem; }
  thead { position: sticky; top: 0; z-index: 1; }
  th {
    background: #f8f9fa; color: #555; font-weight: 600;
    padding: 6px 10px; text-align: left;
    border-bottom: 2px solid #ddd; white-space: nowrap;
  }
  td {
    padding: 5px 10px; border-bottom: 1px solid #eee;
    white-space: nowrap;
  }
  tr:hover td { background: #e8f0fe; }
  tr.clickable { cursor: pointer; }
  .col-time { color: #888; width: 100px; }
  .col-id { color: #e8710a; font-weight: bold; }
  .col-dlc { color: #888; width: 50px; text-align: center; }
  .col-ext { color: #9334e6; width: 50px; text-align: center; }
  .col-data { color: #188038; }
  .col-count { color: #666; width: 60px; text-align: center; }
  .col-freq { color: #1a73e8; width: 70px; text-align: center; }

  .send-panel {
    position: fixed; left: 0; right: 0; bottom: 0;
    z-index: 5;
    padding: 10px 16px; background: #fff;
    border-top: 1px solid #ddd;
    box-shadow: 0 -1px 3px rgba(0,0,0,0.06);
    padding-bottom: calc(10px + env(safe-area-inset-bottom, 20px));
  }
  .send-row {
    display: flex; align-items: center; gap: 8px; flex-wrap: wrap;
  }
  .send-row label { color: #555; font-size: 0.8rem; }
  .input-field {
    padding: 5px 8px; border: 1px solid #ccc; border-radius: 4px;
    background: #fff; color: #333; font-family: inherit;
    font-size: 0.82rem;
  }
  .input-field:focus { border-color: #1a73e8; outline: none; box-shadow: 0 0 0 2px rgba(26,115,232,0.2); }
  #sendId { width: 100px; }
  #sendDlc { width: 50px; }
  #sendData { width: 220px; }
  .checkbox-label {
    display: flex; align-items: center; gap: 4px;
    color: #555; font-size: 0.8rem; cursor: pointer;
  }
  .msg-count { color: #888; font-size: 0.75rem; }
  .top-tabs {
    display: flex; gap: 6px; padding: 6px 12px;
    background: #fff; border-bottom: 1px solid #ddd;
    position: relative; z-index: 1;
  }
  .top-tabs .tab { padding: 5px 16px; font-size: 0.8rem; }
  /* 设备端录制按钮已移除（前端记录器 #curveRecBtn.recording 在下方） */
  @keyframes recblink { 50% { opacity: 0.65; } }
  .hidden { display: none !important; }

  /* ===== 自定义曲线 / 信号定义 ===== */
  .rowline { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; margin: 8px 0; }
  .grow { flex: 1; }
  .sig-card {
    background: #fff; border: 1px solid #e3e6ea; border-radius: 8px;
    padding: 6px 10px; margin: 8px 12px;
  }
  .sig-title { font-size: 0.85rem; }
  .sigrow {
    display: flex; align-items: center; gap: 10px;
    padding: 7px 4px; border-bottom: 1px solid #f2f2f2;
    font-size: 0.78rem; flex-wrap: wrap;
  }
  .sigrow:last-child { border-bottom: none; }
  .cdot { width: 12px; height: 12px; border-radius: 3px; flex: none; }
  .signame { font-weight: 600; min-width: 90px; }
  .sigdef { color: #888; }
  .sigval { margin-left: auto; font-weight: 600; color: #188038; }
  .charts-bar { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; padding: 8px 12px 0; }
  .win-label { color: #555; font-size: 0.78rem; display: flex; align-items: center; gap: 4px; }
  .charts-bar select {
    font-family: inherit; font-size: 0.78rem;
    padding: 4px 6px; border: 1px solid #ccc; border-radius: 4px; background: #fff;
  }
  .strip {
    background: #fff; border: 1px solid #e3e6ea; border-radius: 8px;
    padding: 8px 10px; margin: 8px 12px;
  }
  .strip-head { display: flex; align-items: center; gap: 10px; font-size: 0.78rem; margin-bottom: 4px; }
  .strip canvas { display: block; }
  .hint { color: #888; font-size: 0.72rem; padding: 2px 12px 4px; }
  .empty { color: #888; font-size: 0.78rem; padding: 14px 8px; text-align: center; }
  #curveRecBtn.recording { background: #ea4335; color: #fff; border-color: #ea4335;
                           animation: recblink 1.2s infinite; }

  /* 添加/编辑信号模态框 */
  .modal-mask {
    position: fixed; inset: 0; background: rgba(0,0,0,.35); z-index: 50;
    display: flex; align-items: center; justify-content: center;
  }
  .modal {
    background: #fff; border-radius: 10px; padding: 16px;
    width: min(420px, 94vw); max-height: 92vh; overflow: auto;
  }
  .modal h3 { font-size: 0.9rem; margin-bottom: 12px; }
  .fgrid {
    display: grid; grid-template-columns: 110px 1fr; gap: 8px;
    align-items: center; font-size: 0.78rem;
  }
  .fgrid label { color: #555; }
  .fgrid input[type=text], .fgrid input[type=number], .fgrid select {
    width: 100%; padding: 5px 7px; border: 1px solid #ccc; border-radius: 4px;
    font-family: inherit; font-size: 0.8rem; background: #fff;
  }
  .mt { margin-top: 14px; }
  .modal .btn { padding: 6px 14px; }

  /* 轻提示 */
  .toast {
    position: fixed; left: 50%; bottom: 86px; transform: translateX(-50%);
    background: #333; color: #fff; padding: 8px 16px; border-radius: 18px;
    font-size: 0.78rem; z-index: 99; opacity: 0; transition: opacity .25s;
    pointer-events: none;
  }
  .toast.show { opacity: .95; }

  /* 触屏点击高亮与字体缩放 */
  button { touch-action: manipulation; }
  html { -webkit-text-size-adjust: 100%; }

  /* 手机窄屏适配 */
  @media (max-width: 640px) {
    .header { padding: 6px 10px; flex-wrap: wrap; row-gap: 4px; }
    .header h1 { font-size: 0.9rem; }
    .header-right { gap: 6px; flex-wrap: wrap; }
    .msg-count { display: none; }          /* 窄屏隐藏消息计数 */
    .top-tabs { padding: 5px 8px; }
    .top-tabs .tab { padding: 5px 12px; font-size: 0.78rem; }
    .sig-card, .strip { margin: 6px 8px; padding: 6px 8px; }
    .sigdef { display: none; }               /* 窄屏隐藏信号定义长文本 */
    .hint { padding: 2px 8px 4px; }
    .fgrid { grid-template-columns: 96px 1fr; }
    .send-panel { padding: 8px 10px; }
    #sendId { width: 84px; }
    #sendData { flex: 1; min-width: 120px; }
    .btn { padding: 6px 10px; }
    .col-time { width: auto; }
  }
</style>
</head>
<body>)rawliteral"
