// Sparkle 动态配置示例：通过循环生成代理节点。
// ConfigManager.loadConfig("config.js") 会读取 module.exports。

const regions = ["us", "jp", "sg"]
const proxies = regions.map((region, index) => ({
  name: "demo-" + region,
  type: "socks5",
  server: region + ".example.com",
  port: 1080 + index,
  username: "sparkle",
  password: "change-me"
}))

module.exports = {
  "mixed-port": 7890,
  mode: "rule",
  proxies: proxies,
  "proxy-groups": [
    { name: "Auto", type: "url-test", proxies: proxies.map(p => p.name), url: "https://www.gstatic.com/generate_204" }
  ],
  rules: ["MATCH,Auto"]
}
