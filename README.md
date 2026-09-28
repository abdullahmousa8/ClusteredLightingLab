# ClusteredLightingLab

> **English summary.** A reference implementation of a **clustered
> forward+/deferred lighting** system built on OpenGL 4.3 compute shaders and a
> GPU-driven SoA (Structure-of-Arrays) scene database, based on the research
> paper supplied for this project. It is a **standalone, isolated lab** that
> shares no code with the main engine, intended as a verified reference to be
> ported later.
>
> **State:** cluster grid, Arvo culling, SoA database, spot lights and
> normal-cone back-face math are implemented. **32 host correctness checks pass
> with no GPU required.** Performance has **not** been measured — the build
> agent's environment has no interactive desktop, so no OpenGL context can be
> created there. Run `ClusteredLightingLab.exe` from a real desktop session.
>
> The `src/tests/` directory is not ceremony: most bugs found while writing this
> were math bugs that produced no GL error and no crash, only wrong lighting.
> Property tests against a brute-force reference caught what code review and
> running the program did not.
>
> Build with `build.bat` (MSVC, no external dependencies). Windows only for
> now; the GL bootstrap uses Win32/WGL directly.

---

## التوثيق بالعربية

نموذج إضاءة عنقودية (Clustered Forward+/Deferred) قائم على Compute Shaders
ومعمارية SoA Scenes، مطابق للورقة البحثية المرفقة.

**هذا المشروع معزول تماماً** ولاshares أي سطر مع `godot-master` أو
`godot-next-engine`. الغرض منه التحقق من الخوارزمية بشكل مستقل قبل نقلها إلى
المحرك الأساسي.

## البناء والتشغيل

```
build.bat                        # يبني كل شيء ثم يشغّل اختبارات الصحة تلقائياً
build\ClusteredLightingLab.exe                    # التشغيل العادي (يحتاج جلسة GPU)
build\ClusteredLightingLab.exe --compile-shaders  # ترجمة المظللات فقط، بلا Compute
build\ClusterMathTests.exe                        # اختبارات الحسابات (بلا GPU)
```

لا يعتمد على أي مكتبة خارجية (لا CMake ولا GLAD ولا GLFW): محمّل GL مكتوب
يدوياً، وحلقة النافذة مبنية على Win32 مباشرة. المطلوب MSVC مع `/std:c++20`.

`--compile-shaders` ضروري: يسمح بالتحقق من المظللات من جلسة لا تدعم Compute،
حيث لا يمكن التحقق منها بغير ذلك. أوقف خطأ GLSL فعلياً أثناء التطوير.


## الميزات المنفَّذة (Paper Coverage)

| ميزة الورقة | الحالة |
|---|---|
| شبكة 3D عناقيد بتقسيم لوغاريتمي | ✅ مع تصحيح 3 أخطاء رياضية |
| خوارزمية أرڤو (كرة/AABB) | ✅ + prefilter على شريحة العمق |
| تخطيط SoA + فصل صفحات الذاكرة | ✅ `positionRange` / `viewPosRange` / `colorIntensity` مستقلة |
| `GL_EXT_buffer_reference` (bindless) | ❌ **غير منفَّذ** — استُخدم SSBO تقليدي. اختياري: الأداء |
| الحجز الذري الموحّد لكل عنقود | ✅ |
| الترميز الثنائي (bitmask) لـ ≤64 ضوء | ❌ **غير منفَّذ** — اختياري للأداء |
| **أضواء الـ Spot** | ✅ `spotDirOuter` + `spotInnerCos` كمصفوفتين SoA منفصلتين |
| **إسقاط النواظم الخلفية** | ✅ مع `NormalCone` واختبار他不 تُسقط إضاءة حقيقية |
| Dispatch غير مباشر | ❌ غير لازم في هذا الحجم |

### لماذا لا يوجد bindless ولا bitmask

كلاهما تحسين أداء، لا صحة. لا واحد منهما يغيّر الصورة الناتجة، وكلاهما يحتاج
`GL_EXT_buffer_reference` أو `atomicOr` على int64، وكلاهما غير متاح على كل
السائقين. **لن أضيفهما قبل قياس فعلي يبرّر تعقيدهما** — لوحة مفيدة أن تُقاس،
لا أن تُخمَّن.

## ما الذي يبقى غير مكتمل

1. **مرحلة GPU لاختصار مخاري النواظم.** المخزن موجود ومربوط ومُختبَر رياضياً،
   لكن لا توجد مرحلة GPU تقرأ الـ G-buffer وتحسب المخاري. حالياً كل العناقيد
   تبدأ `w = 2` (غير صالحة) فلا يقع إسقاط أي ضوء — **آمن لكنه عديم الأثر**.
   هذه المرحلة تحتاج قراءة G-buffer كاملاً (لون، عادي، خشونة، موضع) لا مجرد
   عاديّ.
2. **قياس الزمن على عتاد حقيقي.** يحتاج جهازك.


```
1. G-Buffer      -> RT0 albedo+roughness (RGBA16F), RT1 normal (RGBA16F), depth32F
2. Cluster AABB  -> compute، 8×8 workgroups، بلاطات 8×8 بكسل، تقسيم لوغاريتمي
3. Light Culling -> compute، workgroup واحد لكل عنقود (8×8 = 64 خيط)
4. Resolve       -> deferred fullscreen، يقرأ قائمة كل عنقود
```

## العيوب التي صُحّحت مقارنةً بنسخة الورقة

| # | العيب | التصحيح |
|---|---|---|
| 1 | `build_clusters.comp` يبني الـ AABB من نقطتين فقط، فتغفل أركين من الأركان الثمانية لشبه المنحرف | إسقاط النقاط الأربع على مستويي العمق ثم `min/max` عليها |
| 2 | **`min` و`max` مبدآن بـ `vec3(-1e20)` ثم يُقصّ فقط المحور z — فيبقى الحارس `-1e20` في x/y لبلاطة لا تمتدّ في ذلك المحور، فيصبح الـ AABB بحجم المشهد كله** | البدء من الركن الأول كبذرة، والقصّ على z وحده |
| 3 | **`camMin * (z0 / camMin.z)` يعطي `+z0` لأن `camMin.z` سالب — فتوضع كل عنقود أمام الكاميرا وتفرَّغ شبكة العناقيد كلها** | `camMin * (z0 / zNear)` مع `z = -z0` صراحةً |
| 4 | **الـ prefilter يستخدم `zNear = hi.z` و`zFar = lo.z` (معكوسة) — فيرفض كل ضوء تقريباً** | الفحص مقابل `[lo.z, hi.z]` بشكل صحيح |
| 5 | **ترتيب التكرار في `build_clusters` كان `y → x → z` بينما صيغة الفهرس `x + y·gridX + z·gridX·gridY` تجعل z أبطأ بُعد — فالترتيبان لا يتّفقان** | التكرار `z → y → x` في الـ GLSL وفي المرآة الحاسوبية معاً |
| 6 | `count = s_localCount` دون clamp، وحلقة النقل تقرأ خارج المصفوفة المشتركة | clamp مزدوج: على `kMaxLocalLights` وعلى السعة الفعلية لقائمة الفهارس |
| 7 | 144 خيط (16×9) = تسع warps، آخرها نصف خامل ⇒ ~25% هدر | 64 خيط (8×8) = warpان كاملان |
| 8 | 3456 عنقود × 10000 ضوء = 34.5 مليون اختبار، وكل عنصر يُقرأ من VRAM لكل عنقود (~550 MB/إطار بينما L2 = 4 MB فقط) | بثّ الأضواء في بلاطات 256 عبر الذاكرة المشتركة + prefilter على شريحة العمق |
| 9 | `glDispatchCompute(numClusters,1,1)` — عند 1440p عدد العناقيد 388,800 ويتجاوز الحد 65535 لكل بُعد | dispatch ثلاثي الأبعاد `(gridX, gridY, gridZ)` |
| 10 | تمرير `viewProj` حيث يُتوقع `invProjection` | تمرير `Mat4::Inverse` فعلياً |
| 11 | `pow(x, 4.0)` لكل ضوء في التظليل | `x2 = x*x; x4 = x2*x2` |
| 12 | `glDeleteQueryArrays` غير متاح على بعض السائقين | query واحد عبر `glGenQueries` |
| 13 | buffer الـ clusters معلَّم `readonly` مع أن المظلل يكتب فيه | إزالة `readonly` |

**الأخطاء 2 و3 و4 و5 كانت ستُنتج إضاءة مكسورة تماماً** (شبكة عناقيد فارغة، أو
إضاءة في كل مكان، أو إضاءة خاطئة تماماً). ولا واحد منها يُظهر خطأ GL ولا
يكسر الهندسة — صورة فقط، خاطئة بصمت. اختبار المضيف في `src/tests/` هو ما
كشفها، لا تجربة التشغيل ولا مراجعة الكود بالعين.


## اختبارات الصحة

```
build.bat          # يبني كل شيء ثم يشغّل الاختبارات تلقائياً
```

`build\ClusterMathTests.exe` يتحقق من نفس الصيغ التي تعمل على الـ GPU
(`src/renderer/ClusterMath.cpp` نسخة مطابقة لـ `Shaders.h`):

1. كل عنقود AABB صحيح: مقصوص عند مستوى القريب، `min <= max`، وكل الإحداثيات
   منتهية.
2. **الأهم**: 200,000 حالة عشوائية (ضوء × عنقود حقيقي) تقارن المُحسَّن بمرجع
   دقيق. النتيجة الحالية: `4178 إصابة مرجعية، 0 إيجابي كاذب، 0 سلبي كاذب`.
   الاختبار يفشل إن سقط أي ضوء تأثيره حقيقي.
3. ضوء داخل عنقود لا يُفوَّت أبداً (50,000 حالة).
4. الـ clamp يمنع تجاوز قائمة الفهارس عند تشبّع العنقود.
5. **حارس انحراف GLSL**: الاختبار يقرأ `src/renderer/Shaders.h` و
   `src/renderer/ClusterMath.cpp` نفسيهما ويتأكد أن الصيغ الحسّاسة موجودة
   فعلاً — صيغة `logRatio`، علامات `z` السالبة، اتجاه فحص الـ prefilter،
   الـ clamp، وترتيب التكرار `z → y → x`. لا يثبت سلوك المظلل، لكنه يمنع
   أكثر أنواع الانحراف ضرراً (تغيير الرياضيات بصمت).
6. **الأهم:** لكل بكسل وعمق، العنقود الذي تحسبه `kShadeFrag` يجب أن يكون
   فعلاً العنقود الذي يحوي النقطة المُعاد بناؤها. تُختبر ستة مقاسات شاشة
   منها أبعاد **ليست** من مضاعفات 8، لأن خطأً في حجم البلاطة يظهر عندها
   وحدها. الفشل يعني: البكسل يُضاء بقائمة أضواء غريب.

المُحسَّن والمرجع متطابقان تماماً، أي أن الـ prefilter يوفّر وقتاً دون أن
يخسر أي ضوء.

### ترتيب المصادر

`ClusterMath.h` هو **مصدر واحد** لـ `Mat4` و`Vec3`/`Vec4` و`ClusterGridConfig`
و`ClusterAABB`. لا تُعرَّف هذه الأنواع في أي مكان آخر، وإلا أمكن أن تنحرف
رياضيات المُصيِّر عن الرياضيات التي تغطيها الاختبارات.




## ملاحظات معمارية مهمة

- **SoA**: `lightViewPosRange` (vec4) و `lightColorIntensity` (vec4) مصفوفتان
  منفصلتان. مرحلة الغربلة تقرأ `lightViewPosRange` فقط، فتكون كل قراءة
  cache-line مفيدة بالكامل.
- **تحويل الأضواء لمساحة الكاميرا يحدث مرة واحدة لكل ضوء** (على CPU) وليس مرة
  لكل عنقود — وهذا أهم ربح ممكن في التصميم.
- **الحجز الذري الموحد**: `atomicAdd` واحد لكل عنقود على العدّاد العام، مقابل
  `atomicAdd` محلي لكل خيط على الذاكرة العالمية.
- **حجم قائمة الفهارس**: تُقصّ على `GL_MAX_SHADER_STORAGE_BLOCK_SIZE`، ويُبلَّغ
  الحدّ الناتج كـ `u_MaxLightsPerCluster` إلى المظلل حتى لا يكتب خارجها.

## القياس

يُقاس زمن مرحلة الغربلة فقط عبر `GL_TIME_ELAPSED` (قراءة متأخرة بإطار واحد حتى
لا يتوقف الأنبوب). يُطبع في الطرفية كل ثانية:

```
t=  1.2s  lights=10000  cull= X.XXms
```

### حالة التشغيل على هذا الجهاز

```
GL_VERSION  : 4.6.0 NVIDIA 616.92
GL_RENDERER : NVIDIA GeForce RTX 3070/PCIe/SSE2
GLSL        : 4.60 NVIDIA
```

المظللات تُترجَم بنجاح. **لكنّ الجلسة التي تعمل فيها هذه الأدوات بلا سطح مكتب
تفاعلي**: `GetDC` على نافذة يعيد مقبضاً غير صالح (ويختلف من تشغيل لآخر)، فلا
يمكن إنشاء سياق OpenGL هنا إطلاقاً — لا 4.3 core ولا غيره. لذلك يخرج البرنامج
برسالة واضحة بدل أن ينهار.

**هذا قيد بيئة، لا قيد كود.** شغّل `build\ClusteredLightingLab.exe` من جلسة
سطح مكتب حقيقية على بطاقة RTX 3070 نفسها، وستحصل على القياسات.


## نقاط الدمج مع المحرك لاحقاً

| ملف | ما ينقله |
|---|---|
| `src/renderer/Shaders.h` | المظلات الخمسة كما هي، بلا تغيير تقريباً |
| `src/renderer/ClusteredRenderer.cpp` | منطق تمرير الـ passes وم一刻ات الاستعلام |
| `src/scene/SoALights.h/.cpp` | تخطيط SoA نفسه؛ يُستبدل `GenerateOrbit` بمدخلات المحرك |
| `src/renderer/ClusteredRenderer.h` | `Mat4` — يُستبدل بمكتبة رياضيات المحرك |

المعيار: أن يبقى المحرك يتلك ملكية المخازن (buffers) ودورة حياتها، وأن يقدّم
`Renderer` نفس تسلسل الـ passes دون تسرّب حالة.
