# Модуль !CoreMath! — Криптография и квантовые вычисления

Модуль `!CoreMath!` предоставляет высокоточные математические алгоритмы, функции криптографической защиты данных и симулятор квантовых вентилей для языка GrassGreen. Модуль полностью изолирован от платформенных зависимостей через PAL (Platform Abstraction Layer) и функционирует на Windows, Linux и Bare-Metal.

---

## 1. Криптография и точная арифметика

### sha256(str)
Вычисляет криптографический хэш SHA-256 для входной строки. Возвращает строку из 64 шестнадцатеричных символов в нижнем регистре.

- **Спецификация:** `take hash = sha256("input string")`
- **Особенности:** Полностью автономная реализация без сторонних библиотек. Константы раундов вычисляются на лету при старте.

```grass
int main() {
    take password = "my_secure_password";
    take hash = sha256(password);
    disp("SHA-256: " + hash);
    return 0;
}
```

---

### pow_mod(base, exp, mod)
Выполняет быстрое модулярное возведение в степень `(base ^ exp) % mod` с защитой от переполнения (используется 128-битная промежуточная арифметика). Применяется для шифрования RSA, алгоритмов Диффи-Хеллмана и эллиптической криптографии.

- **Спецификация:** `take r = pow_mod(base, exp, mod)`

```grass
int main() {
    take base = 7;
    take exp = 560;
    take mod = 561;
    take result = pow_mod(base, exp, mod);
    disp("pow_mod result: " + result);
    return 0;
}
```

---

### crypto_rand(byte_count)
Генерирует криптографически стойкие случайные байты. Возвращает массив `GrassArray` с числовыми значениями байтов (от 0 до 255).

- **Платформенная изоляция (PAL):**
  - **Windows:** `CryptGenRandom` / `CryptAcquireContext` (CryptoAPI).
  - **Linux:** Прямое чтение из ядра через `/dev/urandom`.
  - **Bare-Metal:** Аппаратный генератор энтропии `RDRAND` (x86_64) или смесь счётчиков `RDTSC` со сдвигами.

```grass
int main() {
    take bytes = crypto_rand(16); // 16 случайных байт
    take i = 0;
    while (i < 16) {
        disp("Byte " + i + ": " + bytes[i]);
        i = i + 1;
    }
    return 0;
}
```

---

### gcd(a, b)
Находит наибольший общий делитель двух целых чисел по алгоритму Евклида.

```grass
int main() {
    take d = gcd(1071, 462);
    disp("GCD(1071, 462) = " + d); // 21
    return 0;
}
```

---

### pow_e(base, exp)
Вычисляет точную степень числа с плавающей точкой без потери точности и без переполнения в бесконечность.

```grass
int main() {
    take val = pow_e(2.5, 8.0);
    disp("2.5^8 = " + val);
    return 0;
}
```

---

### pi()
Возвращает константу Pi с повышенной точностью (до 40 знаков после запятой).

```grass
int main() {
    take p = pi();
    disp("Pi: " + p);
    return 0;
}
```

---

## 2. Квантовый симулятор (Quantum Circuit)

Модуль моделирует квантовые регистры с комплексными амплитудами состояний |0> и |1>, поддерживая суперпозицию, фазовые сдвиги и квантовую запутанность.

### qb_init(name)
Инициализирует кубит с заданным идентификатором в базовое состояние |0> (амплитуда alpha = 1.0, beta = 0.0).

```grass
take status = qb_init("q0");
```

---

### h(name)
Применяет квантовый вентиль Адамара (Hadamard Gate). Переводит кубит в состояние равновероятной суперпозиции `(|0> + |1>) / sqrt(2)`.

```grass
take status = h("q0");
```

---

### x(name)
Применяет квантовый вентиль Паули-X (квантовое отрицание NOT). Меняет амплитуды состояний |0> и |1> местами.

```grass
take status = x("q0");
```

---

### z(name)
Применяет квантовый вентиль Паули-Z (фазовый сдвиг Phase Flip). Инвертирует фазу состояния |1> на 180 градусов (умножает амплитуду на -1).

```grass
take status = z("q0");
```

---

### cnot(control, target)
Управляемый вентиль CNOT (Controlled NOT). Запутывает два кубита: если контрольный кубит находится в состоянии |1>, целевой кубит инвертируется вентилем X.

```grass
take status = cnot("q_ctrl", "q_target");
```

---

### q_measure(name)
Выполняет измерение кубита с коллапсом волновой функции. Согласно квантовой механике (правилу Борна), вероятность выпадения 1 равна квадрату модуля амплитуды beta (`|beta|^2`). Возвращает классический бит 0 или 1, а кубит переходит в измеренное состояние.

```grass
take bit = q_measure("q0");
```

---

### q_rand(name)
Измеряет состояние кубита и возвращает истинно случайный бит. Если кубит предварительно находился в суперпозиции после вентиля Адамара `h(q)`, результат абсолютно недетерминирован.

```grass
int main() {
    qb_init("qubit_entropy");
    h("qubit_entropy");
    take true_random_bit = q_rand("qubit_entropy");
    disp("Quantum Random Bit: " + true_random_bit);
    return 0;
}
```

---

## Полный пример квантово-криптографического генератора

```grass
int main() {
    disp("=== GrassGreen CoreMath Demo ===");

    // 1. Модулярная арифметика
    take p = 29;
    take g = 2;
    take secret = 15;
    take pub_key = pow_mod(g, secret, p);
    disp("Diffie-Hellman Public Key: " + pub_key);

    // 2. Хэширование SHA-256
    take secret_data = "GrassGreen-CoreMath-Payload";
    take digest = sha256(secret_data);
    disp("SHA-256 Digest: " + digest);

    // 3. Квантовое запутывание (Пара Белла |Phi+>)
    qb_init("alice");
    qb_init("bob");
    h("alice");
    cnot("alice", "bob");

    take bit_alice = q_measure("alice");
    disp("Measured Alice Qubit: " + bit_alice);

    return 0;
}
```
