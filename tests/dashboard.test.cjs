const fs = require("node:fs");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const source = fs.readFileSync("firmware/main/web_server.c", "utf8");
const block = source.split("static const char HTML_PAGE[]")[1].split("static esp_err_t root_get_handler")[0];
const html = [...block.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m => vm.runInNewContext(m[0])).join("");
const script = html.split("<script>")[1].split("</script>")[0];
new vm.Script(script); // Validate the complete embedded browser script.
const points = [];
const ctx = {clearRect(){},beginPath(){},stroke(){},
    moveTo(x,y){points.push(["move",x,y]);},lineTo(x,y){points.push(["line",x,y]);}};
const sandbox = {document:{getElementById(){return {width:440,height:100,getContext(){return ctx;}};}}};
vm.createContext(sandbox);
vm.runInContext(script.slice(0,script.indexOf("setInterval(update,")),sandbox);
sandbox.drawChart("chart",[1,null,2,3],"blue");
assert.deepEqual(points.map(p=>p[0]),["move","move","line"]);
assert.equal(points[0][1],4);
assert.equal(points[1][1],292); // Bucket 2 of 4, not the second compressed point.
assert.equal(points[2][1],436);
console.log("PASS embedded dashboard syntax and chart gaps with fixed time positions");
