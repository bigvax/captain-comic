'use strict';
const canvas = document.getElementById('canvas');
const start = document.getElementById('start');
const status = document.getElementById('status');
const overlay = document.getElementById('overlay');
const notice = document.getElementById('notice');
let runtime, playing = false, stopped = false;
function message(text) { notice.textContent = text; notice.hidden = !text; }
function fail(error) {
	playing = false;
	status.textContent = 'Не удалось запустить игру. Обновите страницу и попробуйте ещё раз.';
	start.textContent = 'Обновить'; start.disabled = false; stopped = true;
	overlay.hidden = false;
	console.error(error);
}
function focusGame() { canvas.focus({preventScroll: true}); }
createCaptainComic({
	canvas,
	noInitialRun: true,
	print: text => console.log(text),
	printErr: text => console.error(text),
	onAbort: fail,
	setStatus: text => { if (!playing) status.textContent = text || 'Подготовка игры…'; },
	ccStorageError: () => message('Браузер не разрешает сохранять данные. Рекорды и настройки будут доступны только до закрытия страницы.'),
	ccStopped: () => {
		playing = false; stopped = true; overlay.hidden = false;
		start.textContent = 'Играть снова'; start.disabled = false;
		status.textContent = 'Игра завершена';
		document.querySelectorAll('[data-action]').forEach(button => { button.disabled = true; });
	}
}).then(module => {
	runtime = module;
	start.textContent = 'Играть'; start.disabled = false;
	status.textContent = 'Клавиатура · Звук · Сохранение рекордов';
}).catch(fail);
start.addEventListener('click', () => {
	if (stopped) { location.reload(); return; }
	if (!runtime || playing) return;
	overlay.hidden = true; playing = true;
	focusGame();
	document.querySelectorAll('[data-action]').forEach(button => { button.disabled = false; });
	try { runtime.callMain([]); } catch (error) {
		// simulate_infinite_loop uses an internal unwind on older Emscripten releases.
		if (error !== 'unwind') fail(error);
	}
});
document.querySelectorAll('[data-action]').forEach(button => button.addEventListener('click', () => {
	if (!playing) return;
	runtime._cc_web_action(Number(button.dataset.action));
	focusGame();
}));
document.getElementById('fullscreen').addEventListener('click', async () => {
	try {
		if (document.fullscreenElement) await document.exitFullscreen();
		else await document.getElementById('screen').requestFullscreen();
		focusGame();
	} catch (error) { message('Полноэкранный режим недоступен в этом браузере.'); }
});
canvas.addEventListener('click', focusGame);
window.addEventListener('keydown', event => {
	if (!playing || document.activeElement !== canvas) return;
	if (['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Space','Backspace','Tab','Insert','Delete','F1','F2','F4','F5','F11'].includes(event.code)) event.preventDefault();
}, {capture: true});
document.addEventListener('visibilitychange', () => {
	if (playing && document.hidden) runtime._cc_web_suspend();
});
