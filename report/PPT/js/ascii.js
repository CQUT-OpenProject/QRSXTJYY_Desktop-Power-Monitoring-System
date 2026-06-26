/* ============== ASCII 点阵呼吸场 · IKB 封面/封底专用 ==============
   sin/cos 二维噪声场驱动字符显隐,营造工业仪表板的"涌动呼吸"质感.
   纯 canvas 2D, mix-blend-mode:screen 让字符在 IKB 底色上自然发亮.
   用法:在需要呼吸场的容器(.canvas-card 或 split .half.b-accent)内首位插入
        <canvas class="ascii-bg" aria-hidden="true">,本脚本会自动扫描并启动. */
(function(){
  const canvases = [...document.querySelectorAll('canvas.ascii-bg')];
  if(!canvases.length) return;

  const PALETTE = '   ...:::---+++***◦◦••▢▣';
  const CELL = 16;
  const FONT_SIZE = 13;

  function setup(c){
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const rect = c.getBoundingClientRect();
    if(rect.width < 4 || rect.height < 4) return false;
    c.width = Math.round(rect.width * dpr);
    c.height = Math.round(rect.height * dpr);
    c.__dpr = dpr;
    c.__w = rect.width;
    c.__h = rect.height;
    const ctx = c.getContext('2d');
    ctx.setTransform(dpr,0,0,dpr,0,0);
    const mono = (getComputedStyle(document.documentElement).getPropertyValue('--mono') || 'JetBrains Mono, monospace').trim();
    ctx.font = `500 ${FONT_SIZE}px ${mono}`;
    ctx.textBaseline = 'top';
    c.__ctx = ctx;
    return true;
  }

  function draw(c, t){
    if(!c.__ctx) return;
    const ctx = c.__ctx, w = c.__w, h = c.__h;
    ctx.clearRect(0, 0, w, h);
    const cols = Math.ceil(w / CELL);
    const rows = Math.ceil(h / CELL);
    for(let r=0; r<rows; r++){
      for(let cc=0; cc<cols; cc++){
        const n = (
          Math.sin(cc * 0.18 + t) +
          Math.sin(r * 0.24 - t * 0.7) +
          Math.sin((cc + r) * 0.12 + t * 0.45) +
          Math.sin(Math.hypot(cc - cols * 0.5, r - rows * 0.5) * 0.16 - t * 0.55)
        ) / 4; // [-1, 1]
        const v = (n + 1) / 2; // [0, 1]
        if(v < 0.22) continue;
        const idx = Math.min(PALETTE.length - 1, Math.floor(v * PALETTE.length));
        const ch = PALETTE[idx];
        if(ch === ' ') continue;
        const alpha = 0.08 + (v - 0.22) * 0.55;
        ctx.fillStyle = `rgba(255,255,255,${alpha.toFixed(3)})`;
        ctx.fillText(ch, cc * CELL, r * CELL);
      }
    }
  }

  function resizeAll(){ canvases.forEach(setup); }
  let pending = null;
  window.addEventListener('resize', ()=>{
    if(pending) cancelAnimationFrame(pending);
    pending = requestAnimationFrame(resizeAll);
  }, {passive:true});

  let t0 = performance.now();
  let frame = 0, asciiRAF = 0, running = false;
  function tick(now){
    if(!running){running=false;asciiRAF=0;return;}
    const t = (now - t0) / 1000 * 0.55;
    frame++;
    canvases.forEach(c=>{
      // 离屏 slide 降帧:每 4 帧渲染一次,在屏 slide 每帧渲染
      const slide = c.closest('.slide');
      const rect = slide ? slide.getBoundingClientRect() : null;
      const onscreen = rect && rect.right > 0 && rect.left < window.innerWidth;
      if(!onscreen && (frame & 3) !== 0) return;
      draw(c, t);
    });
    asciiRAF = requestAnimationFrame(tick);
  }
  function start(){
    if(running) return;
    resizeAll();
    t0 = performance.now();
    frame = 0;
    running = true;
    asciiRAF = requestAnimationFrame(tick);
  }
  function stop(){
    running = false;
    if(asciiRAF) cancelAnimationFrame(asciiRAF);
    if(pending) cancelAnimationFrame(pending);
    asciiRAF = 0;
    pending = null;
    canvases.forEach(c=>{
      if(c.__ctx) c.__ctx.clearRect(0,0,c.__w || 0,c.__h || 0);
    });
  }
  start();
})();