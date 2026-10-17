// OpiPerf — ускоритель Rain World для Orange Pi (программный рендер llvmpipe).
// 1) Игра рендерит в уменьшенную RenderTexture, на экран она растягивается без фильтрации.
// 2) Маленькое окно (растягивает X-сервер через xrandr --scale-from).
// 3) Тяжёлые шейдеры заменяются на простые (Futile/Basic или BasicAdditive).
// Клавиши: F7/F8 — масштаб рендера меньше/больше, F9 — счётчик FPS.
// Отладка: команды в файле <игра>/opiperf.cmd (см. PollCommands).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using BepInEx;
using BepInEx.Configuration;
using UnityEngine;
using UnityEngine.UI;

namespace OpiPerf
{
    [BepInPlugin("opi.perf", "OpiPerf", "0.3.0")]
    public class OpiPerfPlugin : BaseUnityPlugin
    {
        static readonly float[] Scales = { 0.125f, 0.1667f, 0.25f, 0.3333f, 0.5f, 0.75f, 1f };

        ConfigEntry<int> scaleIndex;
        ConfigEntry<bool> showFps;
        ConfigEntry<int> fpsCap;
        ConfigEntry<int> forceW, forceH;
        ConfigEntry<string> killedShaders;

        RenderTexture ownRt;
        int appliedW, appliedH;

        float fpsTimer, fpsShown, logTimer;
        int fpsFrames;
        GUIStyle fpsStyle;

        RainWorld rainWorld;
        // Оригинальные Unity-шейдеры FShader'ов, которые мы подменили (для restore).
        readonly Dictionary<string, Shader> originalShaders = new Dictionary<string, Shader>();
        bool shadersApplied;

        static readonly FieldInfo CameraImageField = typeof(Futile).GetField("_cameraImage", BindingFlags.Instance | BindingFlags.NonPublic);
        static readonly PropertyInfo ScreenRtProp = typeof(FScreen).GetProperty("renderTexture");

        void Awake()
        {
            scaleIndex = Config.Bind("Render", "ScaleIndex", 6, "Индекс масштаба: 0=1/8 1=1/6 2=1/4 3=1/3 4=1/2 5=3/4 6=1");
            showFps = Config.Bind("Render", "ShowFps", true, "Показывать FPS");
            fpsCap = Config.Bind("Render", "FpsCap", 60, "Ограничение FPS (-1 = без ограничения)");
            forceW = Config.Bind("Window", "Width", 0, "Принудительная ширина окна игры (0 = не трогать)");
            forceH = Config.Bind("Window", "Height", 0, "Принудительная высота окна игры");
            killedShaders = Config.Bind("Shaders", "Killed", "", "Шейдеры через запятую, заменяемые на Basic (суффикс + = BasicAdditive)");
            Logger.LogInfo("OpiPerf loaded");
        }

        // OPIPERF_LOWRES=1 (лаунчер `rainworld --cpu`): программный рендер — 1/4 и маленькое окно
        // (OPIPERF_WIN=WxH, по умолчанию 512x300), независимо от конфига (конфиг — для GPU-режима).
        static readonly bool LowRes = Environment.GetEnvironmentVariable("OPIPERF_LOWRES") == "1";
        static readonly int[] LowWin = ParseWin(Environment.GetEnvironmentVariable("OPIPERF_WIN"));
        static int[] ParseWin(string s)
        {
            var p = (s ?? "").Split('x');
            return p.Length == 2 && int.TryParse(p[0], out int w) && int.TryParse(p[1], out int h) ? new[] { w, h } : new[] { 512, 300 };
        }
        float Scale => LowRes ? Scales[2] : Scales[Mathf.Clamp(scaleIndex.Value, 0, Scales.Length - 1)];
        int WantW => LowRes ? LowWin[0] : forceW.Value;
        int WantH => LowRes ? LowWin[1] : forceH.Value;

        void Update()
        {
            if (Input.GetKeyDown(KeyCode.F7)) { scaleIndex.Value = Mathf.Max(0, scaleIndex.Value - 1); appliedW = 0; }
            if (Input.GetKeyDown(KeyCode.F8)) { scaleIndex.Value = Mathf.Min(Scales.Length - 1, scaleIndex.Value + 1); appliedW = 0; }
            if (Input.GetKeyDown(KeyCode.F9)) showFps.Value = !showFps.Value;

            if (Application.targetFrameRate != fpsCap.Value) Application.targetFrameRate = fpsCap.Value;
            if (QualitySettings.vSyncCount != 0) QualitySettings.vSyncCount = 0;

            if (rainWorld == null) rainWorld = FindObjectOfType<RainWorld>();
            if (!shadersApplied && rainWorld != null && rainWorld.Shaders != null && rainWorld.Shaders.ContainsKey("Basic"))
            {
                shadersApplied = true;
                foreach (var entry in ParseKilled()) KillShader(entry.Key, entry.Value);
            }

            PollCommands();
            ApplyWindowSize();
            ApplyRenderScale();
            CountFps();
        }

        // ---------- Шейдеры ----------

        Dictionary<string, bool> ParseKilled()
        {
            var d = new Dictionary<string, bool>();
            foreach (var raw in killedShaders.Value.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries))
            {
                var s = raw.Trim();
                bool additive = s.EndsWith("+");
                d[additive ? s.TrimEnd('+') : s] = additive;
            }
            return d;
        }

        void SaveKilled(Dictionary<string, bool> d)
        {
            killedShaders.Value = string.Join(",", d.Select(kv => kv.Key + (kv.Value ? "+" : "")).ToArray());
        }

        // Подменяет Unity-шейдер у FShader (для новых материалов) и у всех существующих материалов.
        bool KillShader(string name, bool additive)
        {
            if (rainWorld?.Shaders == null || !rainWorld.Shaders.TryGetValue(name, out var fs) || fs.shader == null) return false;
            var repl = rainWorld.Shaders[additive ? "BasicAdditive" : "Basic"].shader;
            if (!originalShaders.ContainsKey(name)) originalShaders[name] = fs.shader;
            var orig = originalShaders[name];
            fs.shader = repl;
            int n = 0;
            foreach (var m in Resources.FindObjectsOfTypeAll<Material>())
                if (m.shader == orig) { m.shader = repl; n++; }
            Logger.LogInfo($"killed shader {name} -> {(additive ? "BasicAdditive" : "Basic")} ({n} materials)");
            return true;
        }

        void RestoreShader(string name)
        {
            if (!originalShaders.TryGetValue(name, out var orig) || !rainWorld.Shaders.TryGetValue(name, out var fs)) return;
            var cur = fs.shader;
            fs.shader = orig;
            foreach (var m in Resources.FindObjectsOfTypeAll<Material>())
                if (m.shader == cur && m.name.Contains(name)) m.shader = orig;
            originalShaders.Remove(name);
            Logger.LogInfo($"restored shader {name}");
        }

        // Пишет в лог, какие шейдеры сейчас реально используются (по материалам).
        void DumpShaders()
        {
            var byShader = new Dictionary<Shader, int>();
            foreach (var m in Resources.FindObjectsOfTypeAll<Material>())
                if (m.shader != null) byShader[m.shader] = byShader.TryGetValue(m.shader, out var c) ? c + 1 : 1;
            var names = rainWorld?.Shaders?.Where(kv => kv.Value.shader != null)
                .GroupBy(kv => kv.Value.shader).ToDictionary(g => g.Key, g => string.Join("/", g.Select(kv => kv.Key).ToArray()));
            foreach (var kv in byShader.OrderByDescending(kv => kv.Value))
                Logger.LogInfo($"shader-in-use {(names != null && names.TryGetValue(kv.Key, out var n) ? n : "?")} [{kv.Key.name}] x{kv.Value}");
        }

        // ---------- Команды ----------

        // opiperf.cmd: "scale N", "cap N", "cam 0|1", "res W H", "kill NAME[+]", "restore NAME", "dump"
        float cmdTimer;
        void PollCommands()
        {
            cmdTimer += Time.unscaledDeltaTime;
            if (cmdTimer < 1f) return;
            cmdTimer = 0f;
            string path = Path.Combine(Paths.GameRootPath, "opiperf.cmd");
            if (!File.Exists(path)) return;
            string[] lines = File.ReadAllLines(path);
            File.Delete(path);
            foreach (var line in lines)
            {
                var p = line.Trim().Split(' ');
                if (p.Length == 0 || p[0].Length == 0) continue;
                int v = 0;
                bool hasNum = p.Length > 1 && int.TryParse(p[1], out v);
                switch (p[0])
                {
                    case "scale": if (hasNum) { scaleIndex.Value = v; appliedW = 0; } break;
                    case "cap": if (hasNum) fpsCap.Value = v; break;
                    case "cam": if (hasNum && Futile.instance != null) Futile.instance.camera.enabled = v != 0; break;
                    case "res":
                        if (hasNum) { forceW.Value = v; forceH.Value = p.Length > 2 && int.TryParse(p[2], out int hh) ? hh : 0; }
                        break;
                    case "kill":
                        if (p.Length > 1)
                        {
                            bool add = p[1].EndsWith("+");
                            string nm = p[1].TrimEnd('+');
                            if (KillShader(nm, add)) { var d = ParseKilled(); d[nm] = add; SaveKilled(d); }
                        }
                        break;
                    case "restore":
                        if (p.Length > 1) { RestoreShader(p[1]); var d = ParseKilled(); d.Remove(p[1]); SaveKilled(d); }
                        break;
                    case "dump": DumpShaders(); break;
                    case "story": StartStory(); break;
                }
                Logger.LogInfo("cmd: " + line);
            }
        }

        // Запуск сюжета за Survivor (продолжение, если есть сейв) — для замеров без клавиатуры.
        void StartStory()
        {
            var manager = rainWorld?.processManager;
            if (manager == null) return;
            var cat = SlugcatStats.Name.White;
            manager.rainWorld.inGameSlugCat = cat;
            manager.arenaSitting = null;
            manager.rainWorld.progression.currentSaveState = null;
            manager.rainWorld.progression.miscProgressionData.currentlySelectedSinglePlayerSlugcat = cat;
            if (manager.rainWorld.progression.IsThereASavedGame(cat))
            {
                manager.menuSetup.startGameCondition = ProcessManager.MenuSetup.StoryGameInitCondition.Load;
            }
            else
            {
                manager.rainWorld.progression.WipeSaveState(cat);
                manager.menuSetup.startGameCondition = ProcessManager.MenuSetup.StoryGameInitCondition.New;
            }
            manager.RequestMainProcessSwitch(ProcessManager.ProcessID.Game);
        }

        // ---------- Окно и рендер-таргет ----------

        // Маленький backbuffer: Unity/wined3d гоняют по нему несколько полноэкранных проходов
        // на CPU. Растягивает на монитор X-сервер (xrandr --scale-from, GPU через glamor).
        float resTimer;
        void ApplyWindowSize()
        {
            if (WantW <= 0 || WantH <= 0) return;
            resTimer += Time.unscaledDeltaTime;
            if (resTimer < 1f) return;
            resTimer = 0f;
            if (Screen.width != WantW || Screen.height != WantH || Screen.fullScreenMode != FullScreenMode.FullScreenWindow)
            {
                Logger.LogInfo($"window {Screen.width}x{Screen.height} -> {WantW}x{WantH}");
                Screen.SetResolution(WantW, WantH, FullScreenMode.FullScreenWindow);
            }
        }

        void ApplyRenderScale()
        {
            var futile = Futile.instance;
            if (futile == null || futile.camera == null || Futile.screen == null) return;

            var screen = Futile.screen;
            int w = Mathf.Max(64, Mathf.RoundToInt(screen.pixelWidth * Scale));
            int h = Mathf.Max(48, Mathf.RoundToInt(screen.pixelHeight * Scale));

            // Игра пересоздаёт свою RT при смене разрешения — тогда переустанавливаем свою.
            bool ours = futile.camera.targetTexture == ownRt && ownRt != null;
            if (ours && w == appliedW && h == appliedH) return;

            var old = ownRt;
            ownRt = new RenderTexture(w, h, 0) { filterMode = FilterMode.Point, name = "OpiPerfRT" };
            ownRt.Create();

            ScreenRtProp.SetValue(screen, ownRt, null);
            futile.camera.targetTexture = ownRt;
            if (futile.camera2 != null) futile.camera2.targetTexture = ownRt;
            var img = CameraImageField.GetValue(futile) as RawImage;
            if (img != null) img.texture = ownRt;

            if (old != null) { old.Release(); Destroy(old); }
            appliedW = w; appliedH = h;
            Logger.LogInfo($"render target {w}x{h} (scale {Scale:0.###}, logical {screen.pixelWidth}x{screen.pixelHeight})");
        }

        // ---------- FPS ----------

        int lastGc;
        void CountFps()
        {
            float dt = Time.unscaledDeltaTime;
            int gc = GC.CollectionCount(0);
            if (dt > 0.1f)
                Logger.LogInfo($"spike {dt * 1000f:0}ms gc+{gc - lastGc} heap {GC.GetTotalMemory(false) / 1048576}MB");
            lastGc = gc;
            fpsFrames++;
            fpsTimer += Time.unscaledDeltaTime;
            if (fpsTimer >= 0.5f)
            {
                fpsShown = fpsFrames / fpsTimer;
                fpsFrames = 0;
                fpsTimer = 0f;
            }
            logTimer += Time.unscaledDeltaTime;
            if (logTimer >= 5f)
            {
                logTimer = 0f;
                Logger.LogInfo($"fps {fpsShown:0.0} rt {appliedW}x{appliedH} win {Screen.width}x{Screen.height}");
            }
        }

        void OnGUI()
        {
            if (!showFps.Value) return;
            if (fpsStyle == null)
                fpsStyle = new GUIStyle(GUI.skin.label) { fontSize = 14, normal = { textColor = Color.yellow } };
            GUI.Label(new Rect(4, 2, 240, 22), $"{fpsShown:0} FPS", fpsStyle);
        }
    }
}
