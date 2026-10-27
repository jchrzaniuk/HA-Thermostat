import Foundation
import JavaScriptCore
let html = try String(contentsOfFile: "main/portal.html", encoding: .utf8)
let script = html.components(separatedBy: "<script>")[1].components(separatedBy: "</script>")[0]
func run(_ ready: Bool, _ ha: Bool) {
    let context = JSContext()!
    var error: String? = nil
    context.exceptionHandler = { _, exception in error = exception?.toString() }
    context.evaluateScript("""
    var nodes={};
    function node(id){return nodes[id]||(nodes[id]={hidden:true,textContent:'',value:'',disabled:false,
      elements:{ssid:{value:''},uri:{value:''}},values:{},children:[],
      querySelector(){return node(id+'-button')},replaceChildren(){this.children=[]},append(x){this.children.push(x)}})}
    var document={getElementById:node,createElement:()=>({})};
    var location={reload(){}};
    var FormData=function(form){return Object.entries(form.values)};
    var calls=[];
    var fixture={wifi_ready:\(ready),ha_configured:\(ha),ssid:'Home',version:'0.2.0',ip:'192.168.1.42',ha_url:'http://ha.local:8123'};
    var fetch=async function(path,options){calls.push({path,options});return {ok:true,text:async()=>
      path==='/state'?JSON.stringify(fixture):path==='/entities'?JSON.stringify({entities:[{id:'climate.room',name:'Room'}],status:'Polaczono'}):'Saved'}};
    """)
    context.evaluateScript(script)
    func drain() { RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.05)) }
    drain()
    assert(error == nil, error ?? "")
    assert(context.evaluateScript("nodes['wifi-section'].hidden")!.toBool() == ready)
    assert(context.evaluateScript("nodes['ha-section'].hidden")!.toBool() == !ready)
    assert(context.evaluateScript("nodes['entity-section'].hidden")!.toBool() == !ha)
    if !ready {
        context.evaluateScript("nodes.wifi.values={ssid:'Home',password:'password'}; nodes.wifi.onsubmit({preventDefault(){}})")
        drain()
        assert(context.evaluateScript("calls[calls.length-1].path==='/wifi'")!.toBool())
        assert(context.evaluateScript("Object.keys(JSON.parse(calls[calls.length-1].options.body)).join(',')==='ssid,password'")!.toBool())
    }
    assert(context.evaluateScript("nodes.version.textContent==='0.2.0'")!.toBool())
    assert(context.evaluateScript("typeof nodes.ota.onsubmit==='function'")!.toBool())
    if ha && ready { assert(context.evaluateScript("nodes.entities.children.length===1")!.toBool()) }
    assert(error == nil, error ?? "")
}
run(false,false)
run(true,false)
run(true,true)
print("Portal: Wi-Fi only, LAN HA setup, entity selection passed")
