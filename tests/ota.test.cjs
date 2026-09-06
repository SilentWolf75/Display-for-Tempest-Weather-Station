const assert = require("node:assert/strict");
const { uploadFirmware } = require("../firmware/main/ota.js");
let sent;
global.XMLHttpRequest = class {
    constructor() { this.upload={}; this.headers={}; sent=this; }
    open(method,url) { this.method=method;this.url=url; }
    setRequestHeader(k,v) { this.headers[k]=v; }
    send(body) { this.body=body; }
};
(async()=>{
    const file=new Uint8Array([0xe9,1,2,3]); let progress;
    let pending=uploadFirmware(file,"a&? password",n=>progress=n);
    assert.equal(sent.method,"POST");assert.equal(sent.url,"/ota/update");assert.equal(sent.body,file);
    assert.equal(sent.headers["X-OTA-Password"],"a&? password");
    assert.equal(sent.headers["Content-Type"],"application/octet-stream");
    sent.upload.onprogress({lengthComputable:true,loaded:2,total:4});
    assert.equal(progress,50);sent.status=200;sent.onload();await pending;
    pending=uploadFirmware(file,"",()=>{});sent.status=401;sent.responseText="bad password";sent.onload();
    await assert.rejects(pending,/bad password/);
    pending=uploadFirmware(file,"",()=>{});sent.onerror();await assert.rejects(pending,/Connection lost/);
    pending=uploadFirmware(file,"",()=>{});sent.ontimeout();await assert.rejects(pending,/timed out/);
    console.log("PASS raw upload, password header, progress, HTTP/network errors and timeout");
})().catch(error=>{console.error(error);process.exitCode=1;});
