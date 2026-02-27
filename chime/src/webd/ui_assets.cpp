#include "chime/webd_ui_assets.h"

namespace chime::webd {

const std::string& MainPageHtml() {
  static const std::string html = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Chime Web Console Unavailable</title>
  <style>
    body {
      margin: 24px;
      font-family: system-ui, sans-serif;
      line-height: 1.4;
      color: #1f2937;
      background: #f8fafc;
    }
    .card {
      max-width: 680px;
      background: #fff;
      border: 1px solid #d1d5db;
      border-radius: 10px;
      padding: 16px;
    }
    h1 { margin-top: 0; }
    code {
      background: #f3f4f6;
      border: 1px solid #e5e7eb;
      border-radius: 4px;
      padding: 1px 4px;
    }
  </style>
</head>
<body>
  <div class="card">
    <h1>Web UI Unavailable</h1>
    <p>Something went wrong loading the web interface.</p>
    <p>Check that UI assets are built and the <code>CHIME_WEBD_UI_DIST_DIR</code> path is valid, then restart <code>chime-webd</code>.</p>
  </div>
</body>
</html>
)HTML";

  return html;
}

}  // namespace chime::webd
