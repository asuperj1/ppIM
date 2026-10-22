// =============================================================================
//  web/js/config.js
//  pp 前端配置。
//
//  wsUrl：
//    - 留空：自动使用当前页面地址（默认，最省事）
//      例如用 http://xiaojietong.online 打开，就连 ws://xiaojietong.online/ws
//    - 也可以写死成 "wss://xiaojietong.online/ws"（跨域 / 反向代理场景）
// =============================================================================
window.PP_CONFIG = {
    name: 'pp',
    domain: 'pp.xiaojietong.online',
    // 子域名 / 根路径部署时留空；子路径部署（如 /pp/）会自动推断，也可显式填 '/pp'
    basePath: '',
    // 需要写死跨域地址时填，例如 'wss://pp.xiaojietong.online/ws'；
    // 留空 = 当前域名 + basePath + /ws。
    wsUrl: '',
};
