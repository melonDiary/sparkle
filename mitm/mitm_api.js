// Sparkle MITM 规则示例。
// 普通 HTTP 请求会进入 onRequest/onResponse；HTTPS CONNECT 默认保持透明隧道，
// 因此不会在未安装受信任 CA 的情况下解密 HTTPS。
function onRequest(req) {
    req.headers['X-Sparkle-Intercept'] = 'true';
    console.log('request:', req.method, req.url.href);

    // 访问 example.com 时返回一个本地合成响应。
    if (req.url.hostname === 'example.com') {
        return {
            statusCode: 403,
            statusText: 'Blocked by Sparkle',
            headers: { 'Content-Type': 'text/plain; charset=utf-8' },
            body: 'Blocked by Sparkle'
        };
    }

    return req;
}

function onResponse(res) {
    console.log('response:', res.statusCode);
    return res;
}
