#ifndef WEBTHEME_H
#define WEBTHEME_H

#include <string>

const std::string BASE_CSS = R"(
body { 
    background-color: #0b0b0f; 
    color: #ffffff; 
    font-family: 'Segoe UI', sans-serif; 
    text-align: center; 
    padding: 50px; 
    margin: 0;
    min-height: 100vh;
    position: relative;
    z-index: 1;
}

/* Фиксированное фоновое видео */
.bg-video {
    position: fixed;
    top: 50%; left: 50%;
    min-width: 100%; min-height: 100%;
    width: auto; height: auto;
    z-index: -100;
    transform: translateX(-50%) translateY(-50%);
    background-size: cover;
    opacity: 0.15; /* Приглушаем яркость, чтобы текст читался */
    pointer-events: none;
}

button, input[type="text"], input[type="password"], select {
    position: relative;
    z-index: 2;
}

button { 
    background-color: #1a1a24; 
    padding: 15px 30px; 
    font-size: 18px; 
    cursor: pointer; 
    transition: all 0.3s ease; 
    margin: 10px; 
    border: 2px solid transparent;
    color: #ffffff;
    display: inline-flex;
    justify-content: center;
    align-items: center;
}

button:hover { 
    filter: brightness(1.2); 
    transform: translateY(-2px);
}

/* Чекбоксы */
.grass-checkbox {
    display: flex;
    align-items: center;
    gap: 10px;
    cursor: pointer;
    font-size: 16px;
    margin: 10px;
}
.grass-checkbox input {
    accent-color: var(--neon-color, #00ffcc);
    width: 18px; height: 18px;
}

/* Слайдеры / Ползунки */
.grass-slider {
    width: 300px;
    margin: 15px;
    accent-color: var(--neon-color, #a200ff);
}

/* Выпадающие списки */
.grass-drop {
    background: #1a1a24;
    color: #fff;
    border: 1px solid #444;
    padding: 12px 20px;
    font-size: 16px;
    border-radius: 8px;
    outline: none;
    margin: 10px;
    cursor: pointer;
}

/* Изображения */
.grass-img {
    max-width: 100%;
    height: auto;
    margin: 15px;
    display: block;
}

.glass {
    background: rgba(255, 255, 255, 0.03) !important;
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.08) !important;
    box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.5);
}

.neon {
    box-shadow: 0 0 10px var(--neon-color), 0 0 20px var(--neon-color) inset;
    border-color: var(--neon-color) !important;
    color: var(--neon-color);
}

.thick { font-weight: 800; letter-spacing: 1px; }
.circle { border-radius: 50% !important; }

.grass-row { display: flex; flex-direction: row; justify-content: center; align-items: center; gap: 20px; width: 100%; flex-wrap: wrap; margin: 15px 0; }
.grass-col { display: flex; flex-direction: column; justify-content: center; align-items: center; gap: 15px; margin: 15px 0; }
.grass-box { background: rgba(20, 20, 28, 0.6); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 15px; padding: 30px; margin: 20px auto; max-width: 800px; box-shadow: 0 10px 30px rgba(0,0,0, 0.5); }

@keyframes anim-pulse { 0% { transform: scale(1); box-shadow: 0 0 10px var(--neon-color); } 50% { transform: scale(1.05); box-shadow: 0 0 25px var(--neon-color); } 100% { transform: scale(1); box-shadow: 0 0 10px var(--neon-color); } }
@keyframes anim-float { 0% { transform: translateY(0px); } 50% { transform: translateY(-10px); } 100% { transform: translateY(0px); } }
@keyframes anim-spin { 100% { transform: rotate(360deg); } }
@keyframes anim-shake { 0%, 100% { transform: translateX(0); } 25% { transform: translateX(-5px); } 75% { transform: translateX(5px); } }
@keyframes anim-slide { 0% { transform: translateY(50px); opacity: 0; } 100% { transform: translateY(0); opacity: 1; } }
)";

#endif // WEBTHEME_H