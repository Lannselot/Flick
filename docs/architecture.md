# Архитектура Flick

Документ описывает текущую архитектуру приложения. Flick пока представляет собой
небольшое Qt-приложение, основная логика которого сосредоточена в `src/main.cpp`.

## Компоненты приложения

```mermaid
flowchart TB
    User[Пользователь]

    subgraph Flick[Процесс Flick]
        Main[main]
        Window[ViewerWindow]
        Input[Обработчики ввода<br/>клавиатура и drag-and-drop]
        Sequence[Последовательность путей<br/>sequence_ и currentIndex_]
        Workers[QtConcurrent workers]
        Reader[QImageReader]
        Cache[Decoded image cache<br/>512 MB byte budget]
        Image[QImage<br/>декодированные пиксели]
        Pixmap[QPixmap]
        Label[QLabel]
        Viewport[QScrollArea]
        Feedback[Empty state и<br/>временные сообщения]
        Settings[QSettings]

        Main --> Window
        Window --> Input
        Input --> Sequence
        Sequence --> Workers
        Workers --> Reader
        Reader --> Image
        Image --> Cache
        Cache --> Pixmap
        Pixmap --> Label
        Label --> Viewport
        Window --> Feedback
        Window --> Settings
    end

    Files[(Локальные файлы)] --> Sequence
    Files --> Reader
    User --> Input
    Viewport --> User
    Feedback --> User
```

`ViewerWindow` одновременно отвечает за интерфейс, построение списка файлов,
навигацию и загрузку изображения. Отдельного слоя модели или сервиса
декодирования в текущей версии нет.

## Открытие и декодирование изображения

```mermaid
sequenceDiagram
    actor User as Пользователь
    participant Entry as CLI / File Picker / Drop
    participant Window as ViewerWindow
    participant FS as Файловая система
    participant Reader as QImageReader
    participant UI as QLabel / QScrollArea

    User->>Entry: Выбирает изображение
    Entry->>Window: Передаёт путь
    Window->>FS: Проверяет файл и расширение

    alt Открыт один файл
        Window->>FS: Читает файлы его каталога
        FS-->>Window: Список файлов
        Window->>Window: Фильтрация и естественная сортировка
    else Передано несколько файлов
        Window->>Window: Фильтрация, удаление дублей и сортировка
    end

    Window->>Reader: Запускает декодирование через QtConcurrent
    Reader->>Reader: QImageReader(path)
    Window->>Reader: setAutoTransform(true)
    Reader->>FS: Читает закодированные данные
    FS-->>Reader: Байты файла
    Reader->>Reader: Определяет формат и декодирует пиксели
    Reader-->>Window: QImage

    alt Декодирование успешно
        Window->>Window: Добавляет QImage в ограниченный кеш
        Window->>Window: Сверяет путь с последним запросом
        Window->>UI: QPixmap::fromImage(image_)
        Window->>Reader: Предзагружает соседние элементы
        UI-->>User: Показывает изображение
    else Получен пустой QImage
        Window->>UI: Показывает пустое состояние
        UI-->>User: Изображение не отображается
    end
```

`QImageReader::read()` выполняется в пуле рабочих потоков через `QtConcurrent`.
GUI-поток принимает только готовый `QImage`, сверяет его путь с последним
запрошенным элементом и поэтому игнорирует устаревшие результаты. Предыдущий и
следующий элементы предзагружаются. Декодированные изображения хранятся в кеше
с LRU-вытеснением и бюджетом около 512 МБ.

## Навигация по изображениям

```mermaid
stateDiagram-v2
    [*] --> Empty: запуск без пути
    [*] --> BuildSequence: запуск с путём

    Empty --> BuildSequence: открыть файл или drop
    BuildSequence --> Displayed: последовательность создана<br/>и декодирование успешно
    BuildSequence --> Empty: файл не поддержан<br/>или декодирование не удалось

    Displayed --> DecodePrevious: Left<br/>или готовый результат из кеша
    Displayed --> DecodeNext: Right<br/>или готовый результат из кеша
    DecodePrevious --> Displayed: предыдущий файл загружен
    DecodeNext --> Displayed: следующий файл загружен

    Displayed --> StartBoundary: Left на первом файле
    Displayed --> EndBoundary: Right на последнем файле
    StartBoundary --> Displayed: сообщение исчезает через 1.5 с
    EndBoundary --> Displayed: сообщение исчезает через 1.5 с

    Displayed --> BuildSequence: новый файл или новый drop
```

Список хранится в `sequence_`, а выбранная позиция — в `currentIndex_`.
Переход к соседнему элементу повторно вызывает синхронное декодирование.

## Архитектура тестирования

```mermaid
flowchart LR
    CTest[CTest] --> Test[flick_application_test]
    Test -->|QProcess| App[Отдельный процесс Flick]

    subgraph Isolated[Изолированное временное окружение]
        XDG[XDG config / data / cache / state / runtime]
        Fixtures[Тестовые изображения]
        Screenshot[window.png]
    end

    Test --> Fixtures
    Fixtures --> App
    Test -->|Left, Right, CtrlO,<br/>Drop, Capture через stdin| Harness[Test harness]
    Harness --> App
    App -->|window.grab и save| Screenshot
    Screenshot -->|загрузка как QImage| Test
    Test --> Assertions[Проверка размера,<br/>цветов и состояния процесса]
    XDG --> App
```

Тесты работают через границу настоящего приложения: запускают бинарник в
режиме `offscreen`, имитируют пользовательские события и проверяют итоговый
снимок окна. Тестовый канал команд и создание снимков включаются только при
определении `FLICK_ENABLE_TEST_HARNESS`.
