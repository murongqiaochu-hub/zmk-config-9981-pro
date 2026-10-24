#!/usr/bin/env node
// Local document rendering in one NEW, owned Chrome tab. No device/firmware I/O.
import fs from 'node:fs';
import path from 'node:path';
const argv=process.argv.slice(2), oi=argv.indexOf('--output-dir');
if(oi<0||!argv[oi+1]) throw new Error('--output-dir required');
const root=path.resolve(argv[oi+1]);
const day=new Intl.DateTimeFormat('en-US',{timeZone:'Asia/Shanghai',month:'2-digit',day:'2-digit'}).format(new Date()).replace('/','');
if(path.basename(path.dirname(root))!==day) throw new Error('Use today\'s MMDD task directory');
const html=fs.readFileSync(path.join(root,'03_9981Pro使用说明书_离线阅读版.html'),'utf8');
const base=process.env.MANUAL_CDP_URL||'http://127.0.0.1:16002';
let target,ws,next=1; const pending=new Map();
function cmd(method,params={}){
 const id=next++;
 return new Promise((resolve,reject)=>{
  const timer=setTimeout(()=>{pending.delete(id);reject(new Error('CDP timeout '+method));},120000);
  pending.set(id,{resolve,reject,timer});ws.send(JSON.stringify({id,method,params}));
 });
}
async function evaluate(expression){
 const r=await cmd('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
 if(r.exceptionDetails)throw new Error(JSON.stringify(r.exceptionDetails));
 return r.result.value;
}
try{
 const response=await fetch(base+'/json/new?about:blank',{method:'PUT'});
 if(!response.ok) throw new Error('New render tab failed '+response.status);
 target=await response.json();ws=new WebSocket(target.webSocketDebuggerUrl);
 await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
 ws.addEventListener('message',e=>{
  const m=JSON.parse(e.data);if(!m.id)return;
  const item=pending.get(m.id);if(!item)return;pending.delete(m.id);clearTimeout(item.timer);
  m.error?item.reject(new Error(JSON.stringify(m.error))):item.resolve(m.result);
 });
 await cmd('Page.enable');
 await cmd('Emulation.setDeviceMetricsOverride',{width:760,height:1100,deviceScaleFactor:1,mobile:false});
 await cmd('Emulation.setEmulatedMedia',{media:'print'});
 const tree=await cmd('Page.getFrameTree');
 await cmd('Page.setDocumentContent',{frameId:tree.frameTree.frame.id,html});
 const qa=await evaluate(`document.fonts.ready.then(()=>{
  const pages=[...document.querySelectorAll('.page')].map(p=>{
   const r=p.getBoundingClientRect(),c=p.querySelector('.content').getBoundingClientRect(),f=p.querySelector('.footer').getBoundingClientRect();
   const overflow=[...p.querySelectorAll('.content *')].filter(e=>{const b=e.getBoundingClientRect();return b.right>r.right-10||b.left<r.left-1;}).map(e=>e.tagName+':'+e.textContent.slice(0,25));
   return {id:p.id,clearance:f.top-c.bottom,content_height:c.height,font_loaded:document.fonts.check('13px ManualHan'),horizontal_overflow:overflow};
  });
  return {title:document.title,pages,diagrams:[...document.querySelectorAll('.map svg')].map(s=>({keys:s.querySelectorAll('g[data-key]').length,overflow:[...s.querySelectorAll('g[data-key]')].flatMap(g=>{const r=g.querySelector('rect').getBoundingClientRect();return [...g.querySelectorAll('text')].filter(t=>{const b=t.getBoundingClientRect();return b.left<r.left+1||b.right>r.right-1;}).map(t=>({key:g.getAttribute('data-key'),text:t.textContent}));})})),text:document.body.innerText};
 })`);
 fs.writeFileSync(path.join(root,'05_说明书排版检查.json'),JSON.stringify({...qa,text:undefined},null,2));
 fs.writeFileSync(path.join(root,'06_说明书浏览器正文.txt'),qa.text);
 console.log('LAYOUT',JSON.stringify(qa.pages));
 if(qa.pages.length<10||qa.pages.some(p=>p.clearance<8||!p.font_loaded||p.horizontal_overflow.length))throw new Error('Layout overflow/font issue; fix canonical prose or CSS before printing');
 if(qa.diagrams.length!==4||qa.diagrams.some(d=>d.keys!==42||d.overflow.length))throw new Error('Key count/label overflow: '+JSON.stringify(qa.diagrams));
 const result=await cmd('Page.printToPDF',{printBackground:true,preferCSSPageSize:true,displayHeaderFooter:false,generateTaggedPDF:true,generateDocumentOutline:true,transferMode:'ReturnAsBase64'});
 const pdf=path.join(root,'01_9981Pro使用说明书_完整版.pdf');fs.writeFileSync(pdf,Buffer.from(result.data,'base64'));
 await cmd('Emulation.setDeviceMetricsOverride',{width:760,height:1100,deviceScaleFactor:3,mobile:false});
 const imageDir=path.join(root,'说明书图示');fs.mkdirSync(imageDir,{recursive:true});
 const names=['01_普通层','02_SYM层','03_aA层','04_MEDIA层'];
 async function shoot(selector,file){
  const clip=await evaluate(`(()=>{const el=document.querySelector(${JSON.stringify(selector)});const r=el.getBoundingClientRect();return {x:r.left+scrollX,y:r.top+scrollY,width:r.width,height:r.height,scale:1};})()`);
  const shot=await cmd('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,fromSurface:true,clip});
  fs.writeFileSync(file,Buffer.from(shot.data,'base64'));
 }
 for(let i=0;i<4;i++)await shoot(`.map:has(svg[aria-label^="${['default','sym','upper','media'][i]}"]) svg`,path.join(imageDir,names[i]+'.png'));
 const previews=path.join(root,'排版预览');fs.mkdirSync(previews,{recursive:true});
 await cmd('Emulation.setDeviceMetricsOverride',{width:760,height:1100,deviceScaleFactor:2,mobile:false});
 for(const n of [1,3,6,7,8,11,13,14,15,16,17,18,21,22,23,24,25,26,27])if(n<=qa.pages.length)await shoot('#p'+String(n).padStart(2,'0'),path.join(previews,String(n).padStart(2,'0')+'_页面.png'));
 console.log('PDF_CREATED',pdf,fs.statSync(pdf).size,'pages',qa.pages.length);
}finally{
 if(ws)ws.close();
 if(target){const r=await fetch(base+'/json/close/'+target.id);if(!r.ok)console.error('Owned tab cleanup failed',r.status);}
 for(const item of pending.values())clearTimeout(item.timer);
}
