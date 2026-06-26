/* =============== WebGL 网格背景 (瑞士风专用) ===============
   极简移动网格 + 微弱点阵叠加,营造"工业感、精准感"
   - 主网格: 缓慢漂移的细线网格
   - 次级: 鼠标附近的极细点阵微扰
   - 颜色: 跟随主题(浅底深线 / 深底亮线),配合 mix-blend-mode
*/
const VS = `attribute vec2 position;void main(){gl_Position=vec4(position,0.0,1.0);}`;

const FS = `precision highp float;
uniform vec2 u_resolution;
uniform float u_time;
uniform vec2 u_mouse;
uniform float u_dark; // 0 = light, 1 = dark
uniform vec3 u_accent;

float gridLine(vec2 uv, float spacing, float thickness){
  vec2 g = abs(fract(uv / spacing) - 0.5);
  float d = min(g.x, g.y);
  return 1.0 - smoothstep(thickness - 0.005, thickness + 0.005, d);
}

float dot2(vec2 p){ return dot(p,p); }

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution.xy;
  float aspect = u_resolution.x / u_resolution.y;
  vec2 p = uv;
  p.x *= aspect;

  // 缓慢平移
  vec2 drift = vec2(u_time * 0.008, u_time * 0.005);
  vec2 gp = p + drift;

  // 主细网格 (大间距)
  float mainGrid = gridLine(gp, 0.12, 0.012);
  // 次级网格 (更细更密)
  float subGrid = gridLine(gp, 0.024, 0.04) * 0.4;

  // 鼠标附近的强化
  vec2 m = u_mouse;
  m.x *= aspect;
  float md = length(p - m);
  float mInfluence = exp(-md * 4.0) * 0.5;

  float gridStrength = (mainGrid + subGrid * 0.5) * (0.45 + mInfluence);

  // 点阵 (作为基底)
  vec2 dotGrid = fract(gp * 50.0) - 0.5;
  float dotMask = 1.0 - smoothstep(0.05, 0.14, length(dotGrid));
  // 用低频噪声调制点阵密度
  float wave = sin(gp.x * 1.4 + u_time * 0.15) * cos(gp.y * 1.6 - u_time * 0.12);
  dotMask *= smoothstep(-0.3, 0.6, wave) * 0.6;

  // 颜色: 浅底用深线条,深底用浅线条;高亮处带 accent 痕迹
  vec3 lineColor = mix(vec3(0.08), vec3(0.92), u_dark);
  vec3 bgColor = mix(vec3(0.97, 0.97, 0.96), vec3(0.06, 0.06, 0.07), u_dark);

  // accent 暗示 (鼠标附近偷渡一点 accent 色)
  vec3 col = bgColor;
  col = mix(col, lineColor, gridStrength * 0.55);
  col = mix(col, lineColor, dotMask * 0.35);
  col = mix(col, u_accent, mInfluence * 0.18);

  gl_FragColor = vec4(col, 1.0);
}`;

const mouse={x:0.5,y:0.5};
addEventListener('mousemove',e=>{mouse.x=e.clientX/innerWidth;mouse.y=1-e.clientY/innerHeight});

function bootGL(canvasId, fsSrc){
  const canvas=document.getElementById(canvasId);
  const gl=canvas.getContext('webgl',{alpha:true,antialias:true,premultipliedAlpha:false});
  if(!gl) return ()=>false;
  const mk=(t,s)=>{const sh=gl.createShader(t);gl.shaderSource(sh,s);gl.compileShader(sh);return sh};
  const prog=gl.createProgram();
  gl.attachShader(prog,mk(gl.VERTEX_SHADER,VS));
  gl.attachShader(prog,mk(gl.FRAGMENT_SHADER,fsSrc));
  gl.linkProgram(prog);gl.useProgram(prog);
  const buf=gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER,buf);
  gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
  const pos=gl.getAttribLocation(prog,'position');
  gl.enableVertexAttribArray(pos);gl.vertexAttribPointer(pos,2,gl.FLOAT,false,0,0);
  const lRes=gl.getUniformLocation(prog,'u_resolution');
  const lT=gl.getUniformLocation(prog,'u_time');
  const lM=gl.getUniformLocation(prog,'u_mouse');
  const lD=gl.getUniformLocation(prog,'u_dark');
  const lA=gl.getUniformLocation(prog,'u_accent');
  const resize=()=>{
    const d=Math.min(window.devicePixelRatio||1,2);
    canvas.width=innerWidth*d;canvas.height=innerHeight*d;
    gl.viewport(0,0,canvas.width,canvas.height);
  };
  addEventListener('resize',resize);resize();

  // 读取 CSS 变量,把 accent 颜色塞进 shader
  function readAccent(){
    const cs = getComputedStyle(document.documentElement);
    const hex = cs.getPropertyValue('--accent').trim() || '#002FA7';
    const m = hex.match(/^#([0-9a-f]{6})$/i);
    if(!m) return [0, 0.18, 0.65];
    const n = parseInt(m[1], 16);
    return [((n>>16)&255)/255, ((n>>8)&255)/255, (n&255)/255];
  }
  let accent = readAccent();
  let dark = 0;

  return (tSec, isDark)=>{
    if(isDark !== undefined) dark = isDark ? 1 : 0;
    accent = readAccent();
    gl.uniform2f(lRes,canvas.width,canvas.height);
    gl.uniform1f(lT,tSec);
    gl.uniform2f(lM,mouse.x,mouse.y);
    gl.uniform1f(lD,dark);
    gl.uniform3f(lA,accent[0],accent[1],accent[2]);
    gl.drawArrays(gl.TRIANGLES,0,6);
    return true;
  };
}
let darkMode=false;
let gridCtrl=null, gridRAF=0, gridT0=Date.now();
function startGrid(){
  if(gridRAF) return;
  if(!gridCtrl) gridCtrl = bootGL('bg-grid',FS);
  if(!gridCtrl) return;
  gridT0=Date.now();
  function loop(){
    const t=(Date.now()-gridT0)/1000;
    gridCtrl(t, darkMode);
    gridRAF=requestAnimationFrame(loop);
  }
  gridRAF=requestAnimationFrame(loop);
}
startGrid();

// =============== 导航 ===============
const deck=document.getElementById('deck');
const slides=deck.querySelectorAll('.slide');
const nav=document.getElementById('nav');
let idx=0,total=slides.length,lock=false;

deck.style.width=(total*100)+'vw';

slides.forEach((s,i)=>{
  const b=document.createElement('button');
  b.className='dot';b.dataset.i=i;b.setAttribute('aria-label','Page '+(i+1));
  b.onclick=()=>go(i);
  nav.appendChild(b);
});

function go(n){
  if(lock)return;
  idx=Math.max(0,Math.min(total-1,n));
  window.__currentSlideIndex = idx;
  deck.style.transform=`translateX(${-idx*100}vw)`;
  nav.querySelectorAll('.dot').forEach((d,i)=>d.classList.toggle('active',i===idx));
  const el=slides[idx];
  const isDark = el.classList.contains('dark') || el.classList.contains('accent');
  document.body.classList.toggle('dark-bg', isDark);
  darkMode = isDark;
  if(window.__playSlide) setTimeout(()=>window.__playSlide(idx), 450);
  lock=true;setTimeout(()=>lock=false,700);
}

/* =============== ESC 索引视图 =============== */
let overviewOn=false;
const ov=document.createElement('div');
ov.id='overview';
ov.style.cssText='position:fixed;inset:0;z-index:100;background:rgba(250,250,248,.96);backdrop-filter:blur(12px);display:none;overflow-y:auto;padding:4vh 4vw';
document.body.appendChild(ov);

function buildOverview(){
  ov.innerHTML='';
  const grid=document.createElement('div');
  grid.style.cssText='display:grid;grid-template-columns:repeat(4,1fr);gap:2vh 1.6vw;max-width:90vw;margin:0 auto';
  slides.forEach((s,i)=>{
    const card=document.createElement('div');
    card.style.cssText='cursor:pointer;overflow:hidden;border:2px solid '+(i===idx?'var(--accent)':'rgba(0,0,0,.12)')+';transition:border-color .2s';
    card.onmouseenter=()=>card.style.borderColor='rgba(0,0,0,.4)';
    card.onmouseleave=()=>card.style.borderColor=i===idx?'var(--accent)':'rgba(0,0,0,.12)';
    const wrap=document.createElement('div');
    const isDark = s.classList.contains('dark') || s.classList.contains('accent');
    wrap.style.cssText='width:100%;aspect-ratio:16/9;overflow:hidden;position:relative;pointer-events:none;background:'+(isDark?'var(--ink)':'var(--paper)');
    const clone=s.cloneNode(true);
    clone.style.cssText='width:100vw;height:100vh;transform:scale('+(1/4.5)+');transform-origin:top left;position:absolute;top:0;left:0;pointer-events:none';
    wrap.appendChild(clone);
    const label=document.createElement('div');
    /* ESC 索引卡 label */
  label.style.cssText='padding:6px 10px;font-family:var(--mono);font-size:14px;letter-spacing:.14em;text-transform:uppercase;color:var(--ink);opacity:.7';
    label.textContent=(i+1)+' / '+total;
    card.appendChild(wrap);
    card.appendChild(label);
    card.onclick=()=>{toggleOverview();go(i)};
    grid.appendChild(card);
  });
  ov.appendChild(grid);
}

function toggleOverview(){
  overviewOn=!overviewOn;
  if(overviewOn){buildOverview();ov.style.display='block';}
  else{ov.style.display='none';}
}

addEventListener('keydown',e=>{
  if(e.key==='Escape'){e.preventDefault();toggleOverview();return;}
  if(overviewOn)return;
  if(e.key==='ArrowRight'||e.key==='PageDown'||e.key===' '||e.key==='ArrowDown'){
    if(window.__pipeAdvance && window.__pipeAdvance()) return;
    go(idx+1);
    return;
  }
  if(e.key==='ArrowLeft'||e.key==='PageUp'||e.key==='ArrowUp')go(idx-1);
  if(e.key==='Home')go(0);
  if(e.key==='End')go(total-1);
});

let tx=0,ty=0;
addEventListener('touchstart',e=>{tx=e.touches[0].clientX;ty=e.touches[0].clientY},{passive:true});
addEventListener('touchend',e=>{
  const dx=(e.changedTouches[0].clientX-tx);
  const dy=(e.changedTouches[0].clientY-ty);
  if(Math.abs(dx)>50&&Math.abs(dx)>Math.abs(dy)){
    if(dx<0 && window.__pipeAdvance && window.__pipeAdvance()) return;
    go(idx+(dx<0?1:-1));
  }
},{passive:true});

const initialSlideParam = new URLSearchParams(location.search).get('slide');
const initialSlide = initialSlideParam ? Number(initialSlideParam) - 1 : 0;
go(Number.isFinite(initialSlide) ? initialSlide : 0);