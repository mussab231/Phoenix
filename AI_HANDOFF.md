# Phoenix — ملف تسليم للذكاء الاصطناعي التالي

> انسخ المحتوى بين الخطين أدناه واعطه لأي ذكاء اصطناعي آخر ليكمل العمل على مشروع Phoenix.

---

## البرومبت الجاهز (انسخ هذا)

```
أنت تتسلم مشروع "Phoenix" — مدير تنزيلات لـ Windows مكتوب بـ C++17 / Qt6 / WinHTTP. مهمتك: فحص المشروع كاملاً، اكتشاف الأخطاء وإصلاحها، ضبط ما لا يعمل بشكل صحيح، وتقييم التطويرات اللازمة.

موقع المشروع: C:\Users\hp User\Documents\Default Project\Phoenix
المستودع البعيد: https://github.com/mussab231/Phoenix (فرع main)
الإصدار الحالي: v1.0.0 (مع تكامل المتصفح المدمج بالـ installer)

أوامر البناء والاختبار (مسارات مطلقة لأن PATH قد لا يحتوي عليها):
- بناء التطوير:  & "C:\msys64\mingw64\bin\ninja.exe" -C "C:\Users\hp User\Documents\Default Project\Phoenix\build"
- بناء الإصدار:  powershell -File "C:\Users\hp User\Documents\Default Project\Phoenix\tools\release\build-release.ps1"
  (يتطلب: $env:ISCC = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe")
- اختبارات ذاتية (كلها يجب أن تطبع OK):
  & ".\build\Phoenix.exe" --self-test
  & ".\build\Phoenix.exe" --self-test-retry
  & ".\build\Phoenix.exe" --self-test-urlmatch
  & ".\build\Phoenix.exe" --checksum <file> [expected-sha256]
- Git: & "C:\Program Files\Git\cmd\git.exe" (استعمل المسار الكامل دائماً)

قواعد صارمة:
1. لا تنشئ ملفات توثيق جديدة (README/CHANGELOG) إلا إذا طلبت ذلك صراحة.
2. لا تكتب أي تعليقات في الكود إلا إذا طُلب منك.
3. لا تلتزم (commit) ولا تدفع (push) إلا إذا طُلب منك ذلك صراحة.
4. قبل أي التزام: افحص git status و git diff ورتّب الرسائل بنفس أسلوب المستودع.
5. التزم بأسلوب الكود الحالي (C++17، Qt6 idioms، namespace per module).

الحالة الحالية (موثوقة):
- البناء: أخضر. الاختبارات الذاتية + وحدة Catch2 (53 حالة / 196 تأكيد) كلها OK.
- تكامل المتصفح مكتمل: الإضافة حملت مفتاحاً عاماً ثابتاً (ID: lmjocnjhfnloppdn)،
  والـ installer ينسخها لمجلد browser-extension ويسجل الـ native host تلقائياً
  (install.ps1 -PhoenixExe ...). تشغيل تلقائي للتنزيلات اختياري من قائمة الإضافة.
- أسماء الملفات مشتقة من الخادم: Content-Disposition (مع RFC 5987 UTF-8) ثم مسار
  URL ثم امتداد من Content-Type (MimeMap.cpp، قابلة للاختبار). جرّب: Phoenix --guess <url>
- تم إصلاح علة WinHTTP: استعلام الترويسة الأول مع WINHTTP_NO_OUTPUT_BUFFER يفشل
  دائماً بـ ERROR_INSUFFICIENT_BUFFER (هك نحصل على الطول) — كان يُعتبر فشلاً
  فتُقرأ الترويسات فارغة. أصلِح نفس النمط إن ظهر في أي مكان آخر.
- الواجهة: أيقونات نوع الملف لكل صف، أعمدة تتسع لمحتواها (لا قطع للنص)،
  و"المتبقي" يظهر على شريط التقدم.

المهام المطلوبة منك بالترتيب:
1. فحص شامل للمشروع كاملاً (كل الملفات في src/) بحثاً عن أخطاء فعلية: تسريبات،
   استخدام موارد WinHTTP غير محرّرة، حالات سباق (race conditions)، معالجة أخطاء ناقصة،
   crashes محتملة. أصلِح ما تجده.
2. انتبه لخيط AddDialog::probeFilename المنفصل (std::thread + QPointer): تأكد أنه
   لا يلمس الواجهة بعد تدمير النافذة.
3. شغّل البناء + الاختبارات (الوحدة والذاتية) بعد كل إصلاح وتأكد أنها خضراء.
4. قيّم التطويرات اللازمة واعرضها عليّ مرتبة حسب القيمة قبل تنفيذها.

ابدأ بفحص شامل ثم أبلغني بما وجدت قبل الإصلاح.
```

---

## معلومات إضافية للمرجع (ليست جزءاً من البرومبت)

### هيكل المشروع
```
src/
├── main.cpp                    219 سطر — نقطة الدخول + توزيع CLI
├── app.rc.in                   ملف موارد: IDI_ICON1 ICON "...assets/phoenix.ico"
├── core/
│   ├── HttpClient.cpp/h        880 سطر — WinHTTP، TLS، HEAD/GET، Content-Range، guessFilename
│   ├── MimeMap.cpp/h           210 سطر — أسماء/أنواع الملفات (نقية، قابلة للاختبار)
│   ├── ResumeStore.cpp/h       132 سطر — تخزين بيانات الاستئناف (etag/lastModified)
│   ├── DownloadEngine.cpp/h     98 سطر — محرك الأجزاء
│   ├── DownloadQueue.cpp/h     256 سطر — الطابور + الـ UUID
│   ├── DownloadError.h          39 سطر — تصنيف أخطاء منظّم
│   ├── Checksum.cpp/h           53 سطر — SHA-256
│   ├── SelfTest.cpp/h          970 سطر — اختبارات ذاتية مستخرجة
│   ├── Uuid.cpp/h                9 سطر — UUID v4
│   ├── NativeHost.cpp/h        210 سطر — HTTP listener للتكامل مع المتصفح
│   ├── ClipboardWatcher.cpp/h   40 سطر — مراقبة الحافظة
│   ├── SettingsStore.cpp/h     109 سطر
│   ├── SessionStore.cpp/h      147 سطر
│   ├── PowerControl.cpp/h       53 سطر — منع النوم أثناء التنزيل
│   ├── RateLimiter.cpp/h        61 سطر
│   ├── UrlCodec.cpp/h          105 سطر
│   ├── UrlMatcher.cpp/h         58 سطر
│   ├── ProtocolRegistrar        87 سطر — تسجيل phoenix://
│   ├── SingleInstance.cpp/h     60 سطر
│   └── HttpListener.cpp/h      173 سطر
├── gui/
│   ├── MainWindow.cpp/h        566 سطر
│   ├── AddDialog.cpp/h         165 سطر — sanitizeFileName
│   ├── SettingsDialog.cpp/h    108 سطر
│   ├── I18n.cpp/h              208 سطر — دعم العربية
│   └── theme.qss               136 سطر
└── models/
    ├── DownloadItem.h           26 سطر
    └── DownloadTask.h           22 سطر
```

### سجل التزامات حديث (مرتب زمنياً)
```
18fa75d Replace app icon with the new phoenix artwork
1e4b3c2 Assign a UUID to every queued download
0efb14c Extract self-test suite into core/SelfTest.{h,cpp}
aaff212 Extract self-test suite and native host into SelfTest.cpp
c3fe59a Harden transport: TLS 1.2+ only, block HTTPS->HTTP downgrade
1199b48 Replace string-matching error classification with DownloadError
9e01b57 Add SHA-256 checksum utility and --checksum CLI
4e04cf9 Sanitize suggested filenames for Windows
eb1aa1e Validate ETag/Last-Modified before resuming
262ab22 Add HEAD size fallback with GET byte-range probe
ae09323 Use new phoenix artwork for app icon, atomic resume saves
7e11ce1 Add Arabic UI screenshots and finalize release docs
```

### إصدارات الـ release
- GitHub: `mussab231/Phoenix` — tag `v0.9.0`
- أصول مرفوعة: `Phoenix-0.9.0-portable.zip` + `Phoenix-0.9.0-setup.exe`
- أداة الرفع: `gh.exe` في `C:\Users\hp User\AppData\Local\Temp\opencode\ghcli\bin\gh.exe`
  (التوكن عبر `git credential fill`)

### ملاحظات مهمة
- الواجهة بالعربية — راسل المستخدم بالعربية.
- `--version` غير مُعرَّف حالياً ويُطلق الواجهة الرسومية بدلاً من الطباعة (فكرة تطوير).
- ملف `assets/phoenix-source.png` (1.4MB) هو المصدر الرئيسي للأيقونة.
- أداة `tools/make_icon` تولّد `phoenix-{16,24,32,48,64,128,256}.png` + `phoenix.ico`.
