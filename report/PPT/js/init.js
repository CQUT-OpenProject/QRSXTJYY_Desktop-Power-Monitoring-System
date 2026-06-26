// Windows 平台标记 — 雅黑没有 ExtraLight,需要字重补偿
if(/Win/i.test(navigator.platform || navigator.userAgentData?.platform || '')){
  document.body.classList.add('is-win');
}