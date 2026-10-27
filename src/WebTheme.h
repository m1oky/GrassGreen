#ifndef WEBTHEME_H
#define WEBTHEME_H

#include <string>

const std::string BASE_CSS = R"(
:root {
    --neon-color: #00ffcc;
}

* {
    box-sizing: border-box;
}

body { 
    background-color: #0b0b0f; 
    color: #ffffff; 
    font-family: 'Segoe UI', system-ui, sans-serif; 
    text-align: center; 
    padding: 40px 20px; 
    margin: 0;
    min-height: 100vh;
    position: relative;
    z-index: 1;
    overflow-x: hidden;
    transition: background-color 0.3s ease, color 0.3s ease;
}

.bg-video {
    position: fixed;
    top: 50%; left: 50%;
    min-width: 100%; min-height: 100%;
    width: auto; height: auto;
    z-index: -100;
    transform: translateX(-50%) translateY(-50%);
    object-fit: cover;
    opacity: 0.18;
    pointer-events: none;
}

button, input, select, a, .grass-box {
    position: relative;
    z-index: 2;
    transition: all 0.25s ease;
}

button { 
    background-color: #1a1a24; 
    padding: 14px 28px; 
    font-size: 17px; 
    cursor: pointer; 
    margin: 10px; 
    border: 2px solid transparent;
    border-radius: 8px;
    color: #ffffff;
    display: inline-flex;
    justify-content: center;
    align-items: center;
    gap: 8px;
}

button:hover { 
    filter: brightness(1.2); 
    transform: translateY(-2px);
}

button:active {
    transform: translateY(0px) scale(0.98);
}

.grass-checkbox {
    display: inline-flex;
    align-items: center;
    gap: 10px;
    cursor: pointer;
    font-size: 16px;
    margin: 10px;
    user-select: none;
}
.grass-checkbox input {
    accent-color: var(--neon-color, #00ffcc);
    width: 18px; height: 18px;
    cursor: pointer;
}

.grass-slider {
    width: 300px;
    margin: 15px;
    accent-color: var(--neon-color, #a200ff);
    cursor: pointer;
}

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

.grass-img {
    max-width: 100%;
    height: auto;
    margin: 15px auto;
    display: block;
    transition: all 0.3s ease;
}

.glass {
    background: rgba(255, 255, 255, 0.05) !important;
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
    border: 1px solid rgba(255, 255, 255, 0.12) !important;
    box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.5);
}

.neon {
    box-shadow: 0 0 12px var(--neon-color), 0 0 20px var(--neon-color) inset;
    border-color: var(--neon-color) !important;
    color: var(--neon-color);
}

.thick { font-weight: 800; letter-spacing: 0.8px; }
.circle { border-radius: 50% !important; }

.grass-row { display: flex; flex-direction: row; justify-content: center; align-items: center; gap: 20px; width: 100%; flex-wrap: wrap; margin: 15px 0; }
.grass-col { display: flex; flex-direction: column; justify-content: center; align-items: center; gap: 15px; margin: 15px 0; width: 100%; }
.grass-box { background: rgba(20, 20, 28, 0.7); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 15px; padding: 30px; margin: 20px auto; max-width: 800px; box-shadow: 0 10px 30px rgba(0,0,0, 0.5); }

@keyframes anim-pulse { 0% { transform: scale(1); } 50% { transform: scale(1.05); } 100% { transform: scale(1); } }
@keyframes anim-float { 0% { transform: translateY(0px); } 50% { transform: translateY(-10px); } 100% { transform: translateY(0px); } }
@keyframes anim-spin { 100% { transform: rotate(360deg); } }
@keyframes anim-shake { 0%, 100% { transform: translateX(0); } 25% { transform: translateX(-6px); } 75% { transform: translateX(6px); } }
@keyframes anim-slide { 0% { transform: translateY(40px); opacity: 0; } 100% { transform: translateY(0); opacity: 1; } }
)";

#endif // WEBTHEME_H