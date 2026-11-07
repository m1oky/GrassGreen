# Библиотека `!hardware!` (Аппаратный опрос ПК)

Библиотека `!hardware!` (псевдонимы: `!hw!`, `!sysinfo!`, `!device!`) предоставляет мощный и высокоуровневый синтаксис для опроса всех аппаратных компонентов компьютера: процессора (CPU), оперативной памяти (RAM), материнской платы, BIOS и шины PCI.

Библиотека полностью работает как в режиме виртуальной машины (Windows / Linux), так и в автономном режиме ядра операционной системы (**`--freestanding` / `--kernel` / `--multiboot`**).

---

## Подключение библиотеки

```grass
!GrassGreen!
!hardware!
```

---

## 1. Оперативная память (RAM / ОЗУ)

Функции для проверки общего, занятого и свободного объема оперативной памяти. В режиме ядра (`--kernel`) данные считываются напрямую из таблицы загрузчика **Multiboot** и регистров CMOS.

| Функция | Возвращаемое значение | Описание |
| :--- | :--- | :--- |
| `hw_ram_total()` / `ram_total()` | `int` (МБ) | Общий объем установленной памяти (например, `128` в QEMU, `16384` на ПК) |
| `hw_ram_used()` / `ram_used()` | `int` (МБ) | Объем занятой памяти (ядро, системные буферы) |
| `hw_ram_free()` / `ram_free()` | `int` (МБ) | Свободный объем памяти для приложений |
| `proc_mem` | `int` (МБ) | Встроенное свойство ядра: текущее использование ОЗУ |

### Пример:
```grass
gout << "Всего ОЗУ: " << hw_ram_total() << " МБ" << endl;
gout << "Занято ОЗУ: " << proc_mem << " МБ" << endl;
gout << "Свободно:   " << hw_ram_free() << " МБ" << endl;
```

---

## 2. Процессор (CPU)

Опрос процессора через аппаратную инструкцию **CPUID**:

| Функция | Возвращаемое значение | Описание |
| :--- | :--- | :--- |
| `hw_cpu_vendor()` / `cpu_vendor()` | `string` | Идентификатор производителя (`"GenuineIntel"`, `"AuthenticAMD"`, `"TCGTCGTCGTCG"`) |
| `hw_cpu_brand()` / `cpu_brand()` | `string` | Полное коммерческое название модели процессора |
| `hw_cpu_cores()` / `cpu_cores()` | `int` | Число физических / логических ядер процессора |
| `hw_cpu_family()` / `cpu_family()` | `int` | Семейство микроархитектуры |
| `hw_cpu_freq()` / `cpu_freq()` | `int` (МГц) | Тактовая частота процессора |
| `hw_rdtsc()` / `rdtsc()` | `int` | Чтение аппаратного счетчика тактов процессора (Time Stamp Counter) |

### Пример:
```grass
gout << "Процессор: " << hw_cpu_vendor() << " (" << hw_cpu_brand() << ")" << endl;
gout << "Ядер CPU:  " << hw_cpu_cores() << endl;
```

---

## 3. Материнская плата и BIOS

Чтение таблиц SMBIOS / DMI и системной конфигурации ROM:

| Функция | Возвращаемое значение | Описание |
| :--- | :--- | :--- |
| `hw_board_vendor()` / `board_vendor()` | `string` | Производитель материнской платы (например, `"QEMU"`, `"ASUSTeK"`, `"Dell"`) |
| `hw_board_model()` / `board_model()` | `string` | Модель материнской платы (например, `"Standard PC (i440FX + PIIX)"`) |
| `hw_bios_vendor()` / `bios_vendor()` | `string` | Разработчик BIOS (например, `"SeaBIOS"`, `"American Megatrends"`) |
| `hw_bios_date()` / `bios_date()` | `string` | Дата выпуска версии BIOS (например, `"04/01/2014"`) |

### Пример:
```grass
gout << "Плата: " << hw_board_vendor() << " " << hw_board_model() << endl;
gout << "BIOS:  " << hw_bios_vendor() << " от " << hw_bios_date() << endl;
```

---

## 4. Шина PCI и устройства

| Функция | Возвращаемое значение | Описание |
| :--- | :--- | :--- |
| `hw_pci_count()` / `pci_count()` | `int` | Число обнаруженных PCI устройств на системной шине |

---

## 5. Управление питанием

| Функция | Описание |
| :--- | :--- |
| `hw_reboot()` | Аппаратная перезагрузка компьютера через контроллер клавиатуры / порт `0x64` |

---

## Полный пример системного диспетчера (RQWos):

```grass
!GrassGreen!
!hardware!

int main() {
    take user;
    take cmd;
    use utf - 65001;

    gout << "Как вас зовут: ";
    gin >> user;
    gout << "Здраствуй " << user << ", добро пожаловать в систему RQWos!" << endl;

    while (true) {
        gout << "/";
        gin >> cmd;

        if (cmd == "help") {
            gout << "disp - сведения о системе и памяти" << endl;
            gout << "cpu  - информация о процессоре" << endl;
            gout << "board - данные материнской платы и BIOS" << endl;
            gout << "clear - очистить экран" << endl;
        }

        if (cmd == "disp") {
            gout << "Всего ОЗУ:  " << hw_ram_total() << " МБ" << endl;
            gout << "Занято ОЗУ: " << proc_mem << " МБ" << endl;
            gout << "Свободно:   " << hw_ram_free() << " МБ" << endl;
            gout << "PCI слотов: " << hw_pci_count() << endl;
        }

        if (cmd == "cpu") {
            gout << "ЦПУ:  " << hw_cpu_vendor() << " (" << hw_cpu_brand() << ")" << endl;
            gout << "Ядер: " << hw_cpu_cores() << endl;
        }

        if (cmd == "board") {
            gout << "Плата: " << hw_board_vendor() << " " << hw_board_model() << endl;
            gout << "BIOS:  " << hw_bios_vendor() << " (" << hw_bios_date() << ")" << endl;
        }

        if (cmd == "clear") {
            clear;
        }
    }

    return 0;
}
```
