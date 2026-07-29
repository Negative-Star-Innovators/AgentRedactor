using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;
using FlaUI.Core;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Conditions;
using FlaUI.Core.Definitions;
using FlaUI.Core.Input;
using FlaUI.Core.Tools;
using FlaUI.Core.WindowsAPI;
using FlaUI.UIA3;

namespace FlaUIHelper
{
    internal static class Program
    {
        private const string ProcessName = "AgentRedactor";
        private static readonly TimeSpan ProcessFindTimeout = TimeSpan.FromSeconds(30);
        private static readonly TimeSpan ControlFindTimeout = TimeSpan.FromSeconds(10);
        private static readonly TimeSpan DialogFindTimeout = TimeSpan.FromSeconds(5);

        // Delays are split into a small functional delay (UI stability) plus an
        // optional observation delay that can be re-enabled by setting the
        // FLAUI_OBSERVATION_DELAY_MS environment variable.
        private static readonly int ObservationDelayMs = GetObservationDelayMs();
        private const int ClickDelayMs = 50;
        private const int ToggleDelayMs = 50;
        private const int ListUpdateDelayMs = 50;
        private const int UnfocusDelayMs = 50;
        private const int TypeDelayMs = 50;
        private const int RetryPollIntervalMs = 50;
        private const int WindowPrepareDelayMs = 50;
        private const int WindowVisualStateDelayMs = 50;
        private const int SettleDelayMs = 200;

        private static int GetObservationDelayMs()
        {
            var env = Environment.GetEnvironmentVariable("FLAUI_OBSERVATION_DELAY_MS");
            if (!string.IsNullOrEmpty(env) && int.TryParse(env, out int delay) && delay >= 0)
                return delay;
            return 0;
        }

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        private const int SW_RESTORE = 9;

        private static int Main(string[] args)
        {
            if (args.Length == 0)
            {
                PrintUsage();
                return 1;
            }

            using (var automation = new UIA3Automation())
            {
                try
                {
                    string command = args[0].ToLowerInvariant();
                    switch (command)
                    {
                        case "add-keyword":
                            return AddKeyword(automation, args);
                        case "get-keywords":
                            return GetKeywords(automation);
                        case "delete-keyword":
                            return DeleteKeyword(automation, args);
                        case "toggle-keyword":
                            return ToggleKeyword(automation, args);
                        case "set-keyword-case":
                            return SetKeywordCase(automation, args);
                        case "set-keyword-text":
                            return SetKeywordText(automation, args);
                        case "add-regex":
                            return AddRegex(automation, args);
                        case "get-regexes":
                            return GetRegexes(automation);
                        case "delete-regex":
                            return DeleteRegex(automation, args);
                        case "toggle-regex":
                            return ToggleRegex(automation, args);
                        case "set-regex-text":
                            return SetRegexText(automation, args);
                        case "set-forward-url":
                            return SetForwardUrl(automation, args);
                        case "set-api-key":
                            return SetApiKey(automation, args);
                        case "set-port":
                            return SetPort(automation, args);
                        case "save-profile":
                            return SaveProfile(automation);
                        case "set-enable-logging":
                            return SetEnableLogging(automation, args);
                        case "set-show-sensitive":
                            return SetShowSensitive(automation, args);
                        case "get-is-enabled":
                            return GetIsEnabled(automation, args);
                        case "add-profile":
                            return AddProfile(automation, args);
                        case "select-profile":
                            return SelectProfile(automation, args);
                        case "get-profiles":
                            return GetProfiles(automation);
                        case "remove-profile":
                            return RemoveProfile(automation, args);
                        case "clear-statistics":
                            return ClearStatistics(automation);
                        case "clear-session-redactions":
                            return ClearSessionRedactions(automation);
                        case "clear-logs":
                            return ClearLogs(automation);
                        case "set-pii-type":
                            return SetPiiType(automation, args);
                        case "get-pii-master-state":
                            return GetPiiMasterState(automation);
                        case "set-pii-master":
                            return SetPiiMaster(automation, args);
                        case "get-statistics":
                            return GetStatistics(automation);
                        case "get-session-redactions":
                            return GetSessionRedactions(automation);
                        case "get-proxy-status":
                            return GetProxyStatus(automation);
                        case "get-profile-details":
                            return GetProfileDetails(automation);
                        case "get-api-key-visibility":
                            return GetApiKeyVisibility(automation);
                        case "toggle-show-api-key":
                            return ToggleShowApiKey(automation);
                        case "set-master-password":
                            return SetMasterPassword(automation, args);
                        case "change-master-password":
                            return ChangeMasterPassword(automation, args);
                        case "unlock-master-password":
                            return UnlockMasterPassword(automation, args);
                        case "get-change-password-button-state":
                            return GetChangePasswordButtonState(automation);
                        case "get-content-dialog-text":
                            return GetContentDialogText(automation);
                        case "dismiss-content-dialog":
                            return DismissContentDialog(automation);
                        case "quit":
                            return Quit();
                        default:
                            PrintUsage();
                            return 1;
                    }
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine("ERROR: " + ex);
                    return 1;
                }
            }
        }

        private static void PrintUsage()
        {
            Console.WriteLine("Usage:");
            Console.WriteLine("  FlaUIHelper.exe add-keyword --text <keyword> [--case-sensitive true|false]");
            Console.WriteLine("  FlaUIHelper.exe get-keywords");
            Console.WriteLine("  FlaUIHelper.exe delete-keyword --text <keyword>");
            Console.WriteLine("  FlaUIHelper.exe toggle-keyword --text <keyword>");
            Console.WriteLine("  FlaUIHelper.exe set-keyword-case --text <keyword> --case-sensitive true|false");
            Console.WriteLine("  FlaUIHelper.exe set-keyword-text --old <keyword> --new <keyword>");
            Console.WriteLine("  FlaUIHelper.exe add-regex --pattern <regex>");
            Console.WriteLine("  FlaUIHelper.exe get-regexes");
            Console.WriteLine("  FlaUIHelper.exe delete-regex --text <regex>");
            Console.WriteLine("  FlaUIHelper.exe toggle-regex --text <regex>");
            Console.WriteLine("  FlaUIHelper.exe set-regex-text --old <regex> --new <regex>");
            Console.WriteLine("  FlaUIHelper.exe set-forward-url --url <url>");
            Console.WriteLine("  FlaUIHelper.exe set-api-key --key <key>");
            Console.WriteLine("  FlaUIHelper.exe set-port --port <port>");
            Console.WriteLine("  FlaUIHelper.exe save-profile");
            Console.WriteLine("  FlaUIHelper.exe set-enable-logging --enabled true|false");
            Console.WriteLine("  FlaUIHelper.exe set-show-sensitive --enabled true|false");
            Console.WriteLine("  FlaUIHelper.exe get-is-enabled --id <AutomationId>");
            Console.WriteLine("  FlaUIHelper.exe add-profile --alias <alias>");
            Console.WriteLine("  FlaUIHelper.exe select-profile --alias <alias>");
            Console.WriteLine("  FlaUIHelper.exe get-profiles");
            Console.WriteLine("  FlaUIHelper.exe remove-profile --alias <alias>");
            Console.WriteLine("  FlaUIHelper.exe clear-statistics");
            Console.WriteLine("  FlaUIHelper.exe clear-session-redactions");
            Console.WriteLine("  FlaUIHelper.exe clear-logs");
            Console.WriteLine("  FlaUIHelper.exe set-pii-type --type <type> --enabled true|false");
            Console.WriteLine("  FlaUIHelper.exe get-pii-master-state");
            Console.WriteLine("  FlaUIHelper.exe set-pii-master --enabled true|false");
            Console.WriteLine("  FlaUIHelper.exe get-statistics");
            Console.WriteLine("  FlaUIHelper.exe get-session-redactions");
            Console.WriteLine("  FlaUIHelper.exe get-proxy-status");
            Console.WriteLine("  FlaUIHelper.exe get-profile-details");
            Console.WriteLine("  FlaUIHelper.exe get-api-key-visibility");
            Console.WriteLine("  FlaUIHelper.exe toggle-show-api-key");
            Console.WriteLine("  FlaUIHelper.exe set-master-password --enabled true|false --password <pwd> [--confirm <pwd>]");
            Console.WriteLine("  FlaUIHelper.exe change-master-password --old <pwd> --new <pwd> [--confirm <pwd>]");
            Console.WriteLine("  FlaUIHelper.exe unlock-master-password --password <pwd>");
            Console.WriteLine("  FlaUIHelper.exe get-change-password-button-state");
            Console.WriteLine("  FlaUIHelper.exe get-content-dialog-text");
            Console.WriteLine("  FlaUIHelper.exe dismiss-content-dialog");
            Console.WriteLine("  FlaUIHelper.exe quit");
        }

        private static void InvokeElement(AutomationElement element)
        {
            if (element == null) throw new ArgumentNullException(nameof(element));

            BringIntoView(element);
            WaitForEnabled(element, TimeSpan.FromSeconds(2));

            Exception lastException = null;

            if (element.Patterns.Invoke.IsSupported)
            {
                try
                {
                    Retry.WhileException(
                        () => element.Patterns.Invoke.Pattern.Invoke(),
                        TimeSpan.FromSeconds(2),
                        TimeSpan.FromMilliseconds(RetryPollIntervalMs));
                    Thread.Sleep(ClickDelayMs + ObservationDelayMs);
                    return;
                }
                catch (Exception ex)
                {
                    lastException = ex;
                }
            }

            if (element.Patterns.Toggle.IsSupported)
            {
                try
                {
                    Retry.WhileException(
                        () => element.Patterns.Toggle.Pattern.Toggle(),
                        TimeSpan.FromSeconds(2),
                        TimeSpan.FromMilliseconds(RetryPollIntervalMs));
                    Thread.Sleep(ToggleDelayMs + ObservationDelayMs);
                    return;
                }
                catch (Exception ex)
                {
                    lastException = ex;
                }
            }

            // Fallback to a real click, retrying after scrolling into view if needed.
            try
            {
                element.Click();
                Thread.Sleep(ClickDelayMs + ObservationDelayMs);
                return;
            }
            catch { }

            BringIntoView(element);
            Thread.Sleep(100);
            try
            {
                element.Click();
                Thread.Sleep(ClickDelayMs + ObservationDelayMs);
                return;
            }
            catch { }

            // Last resort: focus the element and press Enter/Space, which works even
            // when the element has no clickable point because it is off-screen.
            try
            {
                element.Focus();
                Thread.Sleep(50);
                Keyboard.Press(VirtualKeyShort.RETURN);
                Thread.Sleep(ClickDelayMs + ObservationDelayMs);
                return;
            }
            catch (Exception ex)
            {
                lastException = ex;
            }

            throw new InvalidOperationException(
                $"Unable to invoke element '{element.Name}' ({element.ControlType}).",
                lastException);
        }

        private static void BringIntoView(AutomationElement element)
        {
            if (element == null) return;
            try
            {
                if (element.Patterns.ScrollItem.IsSupported)
                {
                    element.Patterns.ScrollItem.Pattern.ScrollIntoView();
                }
            }
            catch { }
            try
            {
                element.Focus();
            }
            catch { }
        }

        private static void WaitForEnabled(AutomationElement element, TimeSpan timeout)
        {
            Retry.WhileFalse(
                () => element.IsEnabled,
                timeout,
                TimeSpan.FromMilliseconds(RetryPollIntervalMs));
        }

        private static Process FindAgentRedactorProcess()
        {
            var result = Retry.WhileNull(() =>
            {
                return Process.GetProcessesByName(ProcessName).FirstOrDefault();
            }, ProcessFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;

            if (result == null)
            {
                throw new InvalidOperationException("AgentRedactor.exe process not found.");
            }
            return result;
        }

        private static Window GetMainWindow(UIA3Automation automation)
        {
            var process = FindAgentRedactorProcess();
            var app = Application.Attach(process.Id);
            var window = Retry.WhileNull(() => app.GetMainWindow(automation), ControlFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
            if (window == null)
            {
                throw new InvalidOperationException("AgentRedactor main window not found.");
            }
            return window;
        }

        private static AutomationElement FindByAutomationId(Window window, string id)
        {
            return Retry.WhileNull(() =>
                window.FindFirstDescendant(x => x.ByAutomationId(id)),
                ControlFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static AutomationElement FindByName(Window window, string name)
        {
            return Retry.WhileNull(() =>
                window.FindFirstDescendant(x => x.ByName(name)),
                ControlFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static void BringToForeground(Window window)
        {
            try
            {
                var hwnd = window.Properties.NativeWindowHandle;
                if (hwnd != IntPtr.Zero)
                {
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }
            }
            catch { }
        }

        private static void MaximizeWindow(Window window)
        {
            try
            {
                window.Patterns.Window.Pattern.SetWindowVisualState(WindowVisualState.Maximized);
                Thread.Sleep(WindowVisualStateDelayMs);
            }
            catch { }
        }

        private static void ScrollToBottomWithKeyboard(Window window)
        {
            try
            {
                // Use the main ScrollViewer's ScrollPattern to scroll to the bottom.
                var mainScroll = window.FindFirstDescendant(x => x.ByAutomationId("MainScrollViewer"));
                if (mainScroll != null && mainScroll.Patterns.Scroll.IsSupported)
                {
                    var pattern = mainScroll.Patterns.Scroll.Pattern;
                    int guard = 0;
                    while (guard++ < 50)
                    {
                        double before = pattern.VerticalScrollPercent;
                        if (!double.IsNaN(before) && before >= 99.0)
                            break;
                        pattern.Scroll(ScrollAmount.NoAmount, ScrollAmount.LargeIncrement);
                        Thread.Sleep(50);
                        double after = pattern.VerticalScrollPercent;
                        if (!double.IsNaN(after) && Math.Abs(after - before) < 0.01)
                            break;
                    }
                    Thread.Sleep(100);
                    return;
                }
            }
            catch { }

            try
            {
                // Fallback: send Page Down several times to scroll the main page to the bottom.
                window.Focus();
                Thread.Sleep(50);
                for (int i = 0; i < 30; i++)
                {
                    Keyboard.Press(VirtualKeyShort.NEXT);
                    Thread.Sleep(30);
                }
                using (Keyboard.Pressing(VirtualKeyShort.CONTROL))
                {
                    Keyboard.Press(VirtualKeyShort.END);
                }
                Thread.Sleep(100);
                Thread.Sleep(WindowVisualStateDelayMs);
            }
            catch { }
        }

        private static void ScrollToTopWithKeyboard(Window window)
        {
            try
            {
                window.Focus();
                Thread.Sleep(50);
                for (int i = 0; i < 15; i++)
                {
                    Keyboard.Press(VirtualKeyShort.PRIOR);
                    Thread.Sleep(50);
                }
                Thread.Sleep(WindowVisualStateDelayMs);
            }
            catch { }
        }

        private static void HandleContentDialog(Window window, string primaryButtonName, TimeSpan? timeout = null)
        {
            // ContentDialogs are rendered as a popup of the window. The primary button has
            // AutomationId "PrimaryButton"; wait for it (with the expected name) and click it,
            // then wait for the dialog to close.
            var effectiveTimeout = timeout ?? TimeSpan.FromSeconds(10);
            Thread.Sleep(200);
            var btn = Retry.WhileNull(() =>
                window.FindFirstDescendant(x => x.ByAutomationId("PrimaryButton").And(x.ByName(primaryButtonName))),
                effectiveTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;

            if (btn != null)
            {
                InvokeElement(btn);
                Thread.Sleep(SettleDelayMs + ObservationDelayMs);

                // Wait until the primary button (and therefore the dialog) is gone.
                try
                {
                    Retry.WhileNotNull(() =>
                        window.FindFirstDescendant(x => x.ByAutomationId("PrimaryButton").And(x.ByName(primaryButtonName))),
                        TimeSpan.FromSeconds(5), TimeSpan.FromMilliseconds(RetryPollIntervalMs));
                }
                catch { }
            }
        }

        private static void SetTextBoxValue(TextBox textBox, string value)
        {
            // Prefer the UIA Value pattern: it needs no keyboard focus, so it works
            // on runners where the app window never receives real keyboard focus.
            // Fall back to simulated typing if the pattern is unsupported or the
            // read-back does not match.
            if (!TrySetValuePattern(textBox, value))
            {
                textBox.Focus();
                textBox.Enter(value);
            }
            Thread.Sleep(TypeDelayMs);
        }

        private static bool TrySetValuePattern(TextBox textBox, string value)
        {
            try
            {
                if (!textBox.Patterns.Value.IsSupported)
                    return false;
                textBox.Patterns.Value.Pattern.SetValue(value);
                Thread.Sleep(TypeDelayMs);
                return textBox.Patterns.Value.Pattern.Value == value;
            }
            catch
            {
                return false;
            }
        }

        private static void SetPasswordBoxValue(TextBox passwordBox, string value)
        {
            passwordBox.Focus();
            passwordBox.Enter(value);
            Thread.Sleep(TypeDelayMs);
        }

        private static int AddKeyword(UIA3Automation automation, string[] args)
        {
            string text = null;
            bool caseSensitive = false;
            ParseArgs(args, out text, ref caseSensitive, "--text", "--case-sensitive");

            if (string.IsNullOrEmpty(text))
            {
                Console.Error.WriteLine("ERROR: --text is required.");
                return 1;
            }

            var window = PrepareWindow(automation);

            for (int attempt = 0; attempt < 3; attempt++)
            {
                // Re-find the box each attempt; the element can go stale after the
                // list updates on a slow machine.
                var keywordBox = FindByAutomationId(window, "NewKeywordBox").AsTextBox();

                SetTextBoxValue(keywordBox, text);
                Console.WriteLine("Entered keyword; pausing for observation...");
                Thread.Sleep(ObservationDelayMs);

                SetCaseSensitive(window, caseSensitive);

                // Invoke the Add button instead of pressing RETURN: UIA invocation
                // works even when the window has no real keyboard focus, and it
                // runs the same Click handler as the KeyDown-Enter path.
                var addBtn = FindByAutomationId(window, "AddKeywordBtn").AsButton();
                InvokeElement(addBtn);

                Thread.Sleep(ListUpdateDelayMs + ObservationDelayMs);
                Console.WriteLine("Keyword added; pausing for observation...");

                if (WaitForKeywordRow(window, text, 5000))
                {
                    Console.WriteLine("OK");
                    return 0;
                }
                Console.WriteLine($"Keyword row did not appear after attempt {attempt + 1}; retrying...");
            }

            Console.Error.WriteLine($"ERROR: keyword '{text}' did not appear in the keyword list after 3 attempts.");
            return 1;
        }

        private static bool WaitForKeywordRow(Window window, string text, int timeoutMs)
        {
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            do
            {
                foreach (var (idx, textBox) in FindKeywordTextBoxes(window))
                {
                    if (ExtractKeywordText(textBox, idx).Equals(text, StringComparison.OrdinalIgnoreCase))
                        return true;
                }
                Thread.Sleep(250);
            } while (DateTime.UtcNow < deadline);
            return false;
        }

        private static List<(int Index, AutomationElement TextBox)> FindKeywordTextBoxes(Window window)
        {
            // The C++ app sets AutomationIds of the form KeywordTextBox_<index>_<text> on
            // each keyword row. Return the index and the TextBox element for every keyword row.
            var result = new List<(int Index, AutomationElement TextBox)>();
            var edits = window.FindAllDescendants(x => x.ByControlType(ControlType.Edit));
            foreach (var edit in edits)
            {
                string id = edit.Properties.AutomationId.ValueOrDefault;
                if (string.IsNullOrEmpty(id) || !id.StartsWith("KeywordTextBox_")) continue;
                string rest = id.Substring("KeywordTextBox_".Length);
                int underscore = rest.IndexOf('_');
                string idxPart = underscore >= 0 ? rest.Substring(0, underscore) : rest;
                if (int.TryParse(idxPart, out int idx))
                    result.Add((idx, edit));
            }
            result.Sort((a, b) => a.Index.CompareTo(b.Index));
            return result;
        }

        private static string ExtractKeywordText(AutomationElement textBox, int idx)
        {
            // The AutomationId suffix is set once when the row is created, so it does
            // not reflect inline edits. Read the live TextBox value instead.
            try { return textBox.AsTextBox().Text; }
            catch { }
            try { return textBox.Patterns.Value.Pattern.Value; }
            catch { }
            try { return textBox.Properties.Name.ValueOrDefault; }
            catch { }
            return "";
        }

        private static (int Index, AutomationElement TextBox)? FindKeywordRowByText(Window window, string text)
        {
            return Retry.WhileNull(() =>
            {
                foreach (var (idx, textBox) in FindKeywordTextBoxes(window))
                {
                    string value = ExtractKeywordText(textBox, idx);
                    if (value.Equals(text, StringComparison.OrdinalIgnoreCase))
                        return ((int Index, AutomationElement TextBox)?)(idx, textBox);
                }
                return null;
            }, ControlFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static int GetKeywords(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var rows = FindKeywordTextBoxes(window);
            // The list may still be rendering after a recent add; give it a moment if empty.
            for (int attempt = 0; attempt < 15 && rows.Count == 0; attempt++)
            {
                Thread.Sleep(RetryPollIntervalMs);
                rows = FindKeywordTextBoxes(window);
            }
            Console.WriteLine($"COUNT:{rows.Count}");
            foreach (var (idx, textBox) in rows)
            {
                var enabledCheck = window.FindFirstDescendant(x => x.ByAutomationId($"KeywordCheckBox_{idx}"))?.AsCheckBox();
                var caseBtn = window.FindFirstDescendant(x => x.ByAutomationId($"KeywordCaseButton_{idx}"))?.AsButton();
                if (enabledCheck == null || caseBtn == null) continue;
                bool enabled = enabledCheck.IsChecked.HasValue && enabledCheck.IsChecked.Value;
                string caseText;
                try { caseText = caseBtn.Name; }
                catch { caseText = "No"; }
                string text = ExtractKeywordText(textBox, idx);
                Console.WriteLine($"TEXT:{text}|CASE:{caseText}|ENABLED:{enabled}");
            }
            return 0;
        }

        private static int DeleteKeyword(UIA3Automation automation, string[] args)
        {
            string text = GetArg(args, "--text");
            if (string.IsNullOrEmpty(text))
            {
                Console.Error.WriteLine("ERROR: --text is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var row = FindKeywordRowByText(window, text);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Keyword '{text}' not found in keyword list.");
            }
            int idx = row.Value.Index;
            var deleteBtn = window.FindFirstDescendant(x => x.ByAutomationId($"KeywordDeleteButton_{idx}"))?.AsButton();
            if (deleteBtn == null)
            {
                throw new InvalidOperationException($"Delete button not found for keyword '{text}'.");
            }
            InvokeElement(deleteBtn);
            Thread.Sleep(ListUpdateDelayMs + ObservationDelayMs);

            Console.WriteLine("Keyword deleted; pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int ToggleKeyword(UIA3Automation automation, string[] args)
        {
            string text = GetArg(args, "--text");
            if (string.IsNullOrEmpty(text))
            {
                Console.Error.WriteLine("ERROR: --text is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var row = FindKeywordRowByText(window, text);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Keyword '{text}' not found in keyword list.");
            }
            int idx = row.Value.Index;
            var check = window.FindFirstDescendant(x => x.ByAutomationId($"KeywordCheckBox_{idx}"))?.AsCheckBox();
            if (check == null)
            {
                throw new InvalidOperationException($"Enable checkbox not found for keyword '{text}'.");
            }
            InvokeElement(check);
            Thread.Sleep(ToggleDelayMs + ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int SetKeywordCase(UIA3Automation automation, string[] args)
        {
            string text = GetArg(args, "--text");
            if (string.IsNullOrEmpty(text))
            {
                Console.Error.WriteLine("ERROR: --text is required.");
                return 1;
            }
            bool desiredCase = false;
            bool.TryParse(GetArg(args, "--case-sensitive"), out desiredCase);

            var window = PrepareWindow(automation);
            var row = FindKeywordRowByText(window, text);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Keyword '{text}' not found in keyword list.");
            }
            int idx = row.Value.Index;
            var caseBtn = window.FindFirstDescendant(x => x.ByAutomationId($"KeywordCaseButton_{idx}"))?.AsButton();
            if (caseBtn == null)
            {
                throw new InvalidOperationException($"Case button not found for keyword '{text}'.");
            }
            string caseText;
            try { caseText = caseBtn.Name; }
            catch { caseText = "No"; }
            bool currentCase = caseText == "Yes";
            if (currentCase != desiredCase)
            {
                InvokeElement(caseBtn);
                Thread.Sleep(ToggleDelayMs + ObservationDelayMs);
            }

            Console.WriteLine("OK");
            return 0;
        }

        private static int SetKeywordText(UIA3Automation automation, string[] args)
        {
            string oldText = GetArg(args, "--old");
            string newText = GetArg(args, "--new");
            if (string.IsNullOrEmpty(oldText) || string.IsNullOrEmpty(newText))
            {
                Console.Error.WriteLine("ERROR: --old and --new are required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var row = FindKeywordRowByText(window, oldText);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Keyword '{oldText}' not found in keyword list.");
            }
            var textBox = row.Value.TextBox.AsTextBox();
            if (textBox == null)
            {
                throw new InvalidOperationException($"Text box not found for keyword '{oldText}'.");
            }

            // Edit the existing keyword text box in-place: focus it via UIA
            // SetFocus (no clickable point needed, unlike a mouse Click), set
            // the replacement text (Value pattern, keyboard fallback), then
            // move focus to another field so the TextBox loses focus and the
            // app's LostFocus handler commits it (HomePage.cpp KeywordLostFocus).
            FocusElement(textBox);
            Thread.Sleep(ClickDelayMs);
            Console.WriteLine($"Before: '{textBox.Text}' Focus={textBox.Properties.HasKeyboardFocus.ValueOrDefault}");
            if (!TrySetValuePattern(textBox, newText))
            {
                SelectAllText(textBox);
                Thread.Sleep(TypeDelayMs);
                Keyboard.Type(newText);
                Thread.Sleep(TypeDelayMs);
            }
            Console.WriteLine($"After type: '{textBox.Text}' Focus={textBox.Properties.HasKeyboardFocus.ValueOrDefault}");
            var newKeywordBox = FindByAutomationId(window, "NewKeywordBox");
            Console.WriteLine($"NewKeywordBox found: {newKeywordBox != null}");
            if (newKeywordBox != null)
            {
                FocusElement(newKeywordBox);
                Thread.Sleep(UnfocusDelayMs + ObservationDelayMs);
            }
            Console.WriteLine($"After unfocus: '{textBox.Text}' Focus={textBox.Properties.HasKeyboardFocus.ValueOrDefault}");

            Console.WriteLine("OK");
            return 0;
        }

        private static void SelectAllText(TextBox textBox)
        {
            // Try the Text pattern's document range first (cleanest).
            try
            {
                var textPattern = textBox.Patterns.Text.Pattern;
                var range = textPattern.DocumentRange;
                range.Select();
                return;
            }
            catch { }

            // Fall back to Ctrl+A via keyboard.
            try
            {
                using (Keyboard.Pressing(VirtualKeyShort.CONTROL))
                {
                    Keyboard.Press(VirtualKeyShort.KEY_A);
                }
            }
            catch
            {
                // If Pressing isn't available, move home and Shift+End.
                Keyboard.Press(VirtualKeyShort.HOME);
                using (Keyboard.Pressing(VirtualKeyShort.SHIFT))
                {
                    Keyboard.Press(VirtualKeyShort.END);
                }
            }
        }

        private static void FocusElement(AutomationElement element)
        {
            // Prefer UIA SetFocus: it needs no clickable point and works even
            // when the window has no real keyboard focus. Fall back to a mouse
            // click only if SetFocus is unavailable.
            try
            {
                element.Focus();
                return;
            }
            catch { }
            element.Click();
        }

        // -------------------------------------------------------------------------
        // Regex helpers
        // -------------------------------------------------------------------------

        private static List<(int Index, AutomationElement TextBox)> FindRegexTextBoxes(Window window)
        {
            var result = new List<(int Index, AutomationElement TextBox)>();
            var edits = window.FindAllDescendants(x => x.ByControlType(ControlType.Edit));
            foreach (var edit in edits)
            {
                string id = edit.Properties.AutomationId.ValueOrDefault;
                if (string.IsNullOrEmpty(id) || !id.StartsWith("RegexTextBox_")) continue;
                string rest = id.Substring("RegexTextBox_".Length);
                int underscore = rest.IndexOf('_');
                string idxPart = underscore >= 0 ? rest.Substring(0, underscore) : rest;
                if (int.TryParse(idxPart, out int idx))
                    result.Add((idx, edit));
            }
            result.Sort((a, b) => a.Index.CompareTo(b.Index));
            return result;
        }

        private static string ExtractRegexText(AutomationElement textBox, int idx)
        {
            try { return textBox.AsTextBox().Text; }
            catch { }
            try { return textBox.Patterns.Value.Pattern.Value; }
            catch { }
            try { return textBox.Properties.Name.ValueOrDefault; }
            catch { }
            return "";
        }

        private static (int Index, AutomationElement TextBox)? FindRegexRowByText(Window window, string text)
        {
            return Retry.WhileNull(() =>
            {
                foreach (var (idx, textBox) in FindRegexTextBoxes(window))
                {
                    string value = ExtractRegexText(textBox, idx);
                    if (value.Equals(text, StringComparison.Ordinal))
                        return ((int Index, AutomationElement TextBox)?)(idx, textBox);
                }
                return null;
            }, ControlFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static int GetRegexes(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var rows = FindRegexTextBoxes(window);
            for (int attempt = 0; attempt < 15 && rows.Count == 0; attempt++)
            {
                Thread.Sleep(RetryPollIntervalMs);
                rows = FindRegexTextBoxes(window);
            }
            Console.WriteLine($"COUNT:{rows.Count}");
            foreach (var (idx, textBox) in rows)
            {
                var enabledCheck = window.FindFirstDescendant(x => x.ByAutomationId($"RegexCheckBox_{idx}"))?.AsCheckBox();
                if (enabledCheck == null) continue;
                bool enabled = enabledCheck.IsChecked.HasValue && enabledCheck.IsChecked.Value;
                string text = ExtractRegexText(textBox, idx);
                Console.WriteLine($"PATTERN:{text}");
                Console.WriteLine($"ENABLED:{enabled}");
            }
            return 0;
        }

        private static int DeleteRegex(UIA3Automation automation, string[] args)
        {
            string text = GetArg(args, "--text");
            if (string.IsNullOrEmpty(text))
            {
                Console.Error.WriteLine("ERROR: --text is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var row = FindRegexRowByText(window, text);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Regex '{text}' not found in regex list.");
            }
            int idx = row.Value.Index;
            var deleteBtn = window.FindFirstDescendant(x => x.ByAutomationId($"RegexDeleteButton_{idx}"))?.AsButton();
            if (deleteBtn == null)
            {
                throw new InvalidOperationException($"Delete button not found for regex '{text}'.");
            }
            InvokeElement(deleteBtn);
            Thread.Sleep(ListUpdateDelayMs + ObservationDelayMs);

            Console.WriteLine("Regex deleted; pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int ToggleRegex(UIA3Automation automation, string[] args)
        {
            string text = GetArg(args, "--text");
            if (string.IsNullOrEmpty(text))
            {
                Console.Error.WriteLine("ERROR: --text is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var row = FindRegexRowByText(window, text);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Regex '{text}' not found in regex list.");
            }
            int idx = row.Value.Index;
            var check = window.FindFirstDescendant(x => x.ByAutomationId($"RegexCheckBox_{idx}"))?.AsCheckBox();
            if (check == null)
            {
                throw new InvalidOperationException($"Enable checkbox not found for regex '{text}'.");
            }
            InvokeElement(check);
            Thread.Sleep(ToggleDelayMs + ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int SetRegexText(UIA3Automation automation, string[] args)
        {
            string oldText = GetArg(args, "--old");
            string newText = GetArg(args, "--new");
            if (string.IsNullOrEmpty(oldText) || string.IsNullOrEmpty(newText))
            {
                Console.Error.WriteLine("ERROR: --old and --new are required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var row = FindRegexRowByText(window, oldText);
            if (!row.HasValue)
            {
                throw new InvalidOperationException($"Regex '{oldText}' not found in regex list.");
            }
            var textBox = row.Value.TextBox.AsTextBox();
            if (textBox == null)
            {
                throw new InvalidOperationException($"Text box not found for regex '{oldText}'.");
            }

            // Focus the row text box via UIA SetFocus (no clickable point
            // needed, unlike a mouse Click), set the replacement text, then
            // move focus to another field so the app's LostFocus handler
            // commits the edit (HomePage.cpp RegexLostFocus).
            FocusElement(textBox);
            Thread.Sleep(ClickDelayMs);
            Console.WriteLine($"Before: '{textBox.Text}' Focus={textBox.Properties.HasKeyboardFocus.ValueOrDefault}");
            if (!TrySetValuePattern(textBox, newText))
            {
                SelectAllText(textBox);
                Thread.Sleep(TypeDelayMs);
                Keyboard.Type(newText);
                Thread.Sleep(TypeDelayMs);
            }
            Console.WriteLine($"After type: '{textBox.Text}' Focus={textBox.Properties.HasKeyboardFocus.ValueOrDefault}");
            var newRegexBox = FindByAutomationId(window, "NewRegexBox");
            Console.WriteLine($"NewRegexBox found: {newRegexBox != null}");
            if (newRegexBox != null)
            {
                FocusElement(newRegexBox);
                Thread.Sleep(UnfocusDelayMs + ObservationDelayMs);
            }
            Console.WriteLine($"After unfocus: '{textBox.Text}' Focus={textBox.Properties.HasKeyboardFocus.ValueOrDefault}");

            Console.WriteLine("OK");
            return 0;
        }

        private static int AddRegex(UIA3Automation automation, string[] args)
        {
            string pattern = GetArg(args, "--pattern");
            if (string.IsNullOrEmpty(pattern))
            {
                Console.Error.WriteLine("ERROR: --pattern is required.");
                return 1;
            }

            var window = PrepareWindow(automation);

            for (int attempt = 0; attempt < 3; attempt++)
            {
                // Re-find the box each attempt; the element can go stale after the
                // list updates on a slow machine.
                var regexBox = FindByAutomationId(window, "NewRegexBox").AsTextBox();

                SetTextBoxValue(regexBox, pattern);
                Console.WriteLine("Entered regex; pausing for observation...");
                Thread.Sleep(ObservationDelayMs);

                // Invoke the Add button instead of pressing RETURN: UIA invocation
                // works even when the window has no real keyboard focus, and it
                // runs the same Click handler as the KeyDown-Enter path.
                var addBtn = FindByAutomationId(window, "AddRegexBtn").AsButton();
                InvokeElement(addBtn);

                Thread.Sleep(ListUpdateDelayMs + ObservationDelayMs);
                Console.WriteLine("Regex added; pausing for observation...");

                if (WaitForRegexRow(window, pattern, 5000))
                {
                    Console.WriteLine("OK");
                    return 0;
                }
                Console.WriteLine($"Regex row did not appear after attempt {attempt + 1}; retrying...");
            }

            Console.Error.WriteLine($"ERROR: regex '{pattern}' did not appear in the regex list after 3 attempts.");
            return 1;
        }

        private static bool WaitForRegexRow(Window window, string text, int timeoutMs)
        {
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            do
            {
                foreach (var (idx, textBox) in FindRegexTextBoxes(window))
                {
                    if (ExtractRegexText(textBox, idx).Equals(text, StringComparison.Ordinal))
                        return true;
                }
                Thread.Sleep(250);
            } while (DateTime.UtcNow < deadline);
            return false;
        }

        private static int SetForwardUrl(UIA3Automation automation, string[] args)
        {
            string url = GetArg(args, "--url");
            if (string.IsNullOrEmpty(url))
            {
                Console.Error.WriteLine("ERROR: --url is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var urlBox = FindByAutomationId(window, "UrlBox").AsTextBox();
            SetTextBoxValue(urlBox, url);
            Console.WriteLine("Set forward URL; pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int SetApiKey(UIA3Automation automation, string[] args)
        {
            string key = GetArg(args, "--key");
            if (key == null)
            {
                Console.Error.WriteLine("ERROR: --key is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var keyBox = FindByAutomationId(window, "ApiKeyBox").AsTextBox();
            var showCheck = FindByAutomationId(window, "ShowKeyCheck").AsCheckBox();
            bool wasChecked = showCheck.IsChecked.HasValue && showCheck.IsChecked.Value;

            // ApiKeyBox is a PasswordBox (HomePage.xaml): it only exposes the UIA
            // Value pattern while its reveal mode is Visible (ShowKeyCheck toggles
            // PasswordRevealMode in HomePage.cpp ShowKey_Toggled). Temporarily
            // reveal it so the key can be set without keyboard input, then
            // restore the checkbox to its previous state.
            if (!wasChecked)
            {
                InvokeElement(showCheck);
                Thread.Sleep(SettleDelayMs);
            }

            bool usedValuePattern = TrySetValuePattern(keyBox, key);
            if (!usedValuePattern)
            {
                SetPasswordBoxValue(keyBox, key);
            }

            if (!wasChecked)
            {
                InvokeElement(showCheck);
                Thread.Sleep(SettleDelayMs);
            }

            Console.WriteLine($"Set API key (value pattern: {usedValuePattern}); pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int SetPort(UIA3Automation automation, string[] args)
        {
            string portStr = GetArg(args, "--port");
            if (string.IsNullOrEmpty(portStr))
            {
                Console.Error.WriteLine("ERROR: --port is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var portBox = FindByAutomationId(window, "PortBox").AsTextBox();
            SetTextBoxValue(portBox, portStr);
            Console.WriteLine("Set port; pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int SaveProfile(UIA3Automation automation)
        {
            Exception lastException = null;
            for (int attempt = 0; attempt < 3; attempt++)
            {
                try
                {
                    var window = PrepareWindow(automation);
                    var saveBtn = FindByAutomationId(window, "SaveProfileBtn").AsButton();
                    if (!saveBtn.IsEnabled)
                    {
                        Thread.Sleep(ListUpdateDelayMs);
                        continue;
                    }
                    InvokeElement(saveBtn);
                    Thread.Sleep(ListUpdateDelayMs + ObservationDelayMs);

                    Console.WriteLine("Profile saved; pausing for observation...");
                    Thread.Sleep(ObservationDelayMs);

                    Console.WriteLine("OK");
                    return 0;
                }
                catch (Exception ex)
                {
                    lastException = ex;
                    Console.WriteLine($"SaveProfile attempt {attempt + 1} failed: {ex.Message}");
                    Thread.Sleep(ListUpdateDelayMs);
                }
            }

            throw new InvalidOperationException("Failed to save profile after multiple attempts.", lastException);
        }

        private static int SetEnableLogging(UIA3Automation automation, string[] args)
        {
            bool enabled = false;
            if (!bool.TryParse(GetArg(args, "--enabled"), out enabled))
            {
                Console.Error.WriteLine("ERROR: --enabled is required (true|false).");
                return 1;
            }

            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);

            for (int attempt = 0; attempt < 3; attempt++)
            {
                var check = FindByAutomationId(window, "EnableLoggingCheck").AsCheckBox();
                bool currentlyChecked = check.IsChecked.HasValue && check.IsChecked.Value;
                if (enabled != currentlyChecked)
                {
                    // Invoke raises the app's Click handler, which synchronously
                    // updates ShowSensitiveCheck().IsEnabled (HomePage.cpp
                    // EnableLogging_Click); a mouse Click is unreliable on
                    // runners where the window has no real focus.
                    InvokeElement(check);
                    Thread.Sleep(SettleDelayMs + ObservationDelayMs);
                }

                if (WaitForEnableLoggingState(window, enabled, 5000))
                {
                    Console.WriteLine("Enable logging set; pausing for observation...");
                    Thread.Sleep(ObservationDelayMs);
                    Console.WriteLine("OK");
                    return 0;
                }
                Console.WriteLine($"Enable logging state did not settle after attempt {attempt + 1}; retrying...");
            }

            Console.Error.WriteLine($"ERROR: Enable logging did not reach {(enabled ? "enabled" : "disabled")} after 3 attempts.");
            return 1;
        }

        private static bool WaitForEnableLoggingState(Window window, bool expectedEnabled, int timeoutMs)
        {
            // The setting has landed when the checkbox matches and the app's
            // Click handler has run, which flips ShowSensitiveCheck.IsEnabled.
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            do
            {
                try
                {
                    var check = FindByAutomationId(window, "EnableLoggingCheck")?.AsCheckBox();
                    var sensitive = FindByAutomationId(window, "ShowSensitiveCheck")?.AsCheckBox();
                    if (check != null && sensitive != null)
                    {
                        bool isChecked = check.IsChecked.HasValue && check.IsChecked.Value;
                        if (isChecked == expectedEnabled && sensitive.IsEnabled == expectedEnabled)
                            return true;
                    }
                }
                catch { }
                Thread.Sleep(250);
            } while (DateTime.UtcNow < deadline);
            return false;
        }

        private static int SetShowSensitive(UIA3Automation automation, string[] args)
        {
            bool enabled = false;
            if (!bool.TryParse(GetArg(args, "--enabled"), out enabled))
            {
                Console.Error.WriteLine("ERROR: --enabled is required (true|false).");
                return 1;
            }

            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);

            for (int attempt = 0; attempt < 3; attempt++)
            {
                var check = FindByAutomationId(window, "ShowSensitiveCheck").AsCheckBox();
                bool currentlyChecked = check.IsChecked.HasValue && check.IsChecked.Value;
                if (enabled && !currentlyChecked)
                {
                    // Invoke raises the app's Click handler, which opens the
                    // confirmation ContentDialog; the setting only applies if
                    // Primary is clicked (HomePage.cpp ShowSensitive_Click).
                    InvokeElement(check);
                    Thread.Sleep(SettleDelayMs + ObservationDelayMs);
                    // Confirm the show-sensitive dialog (English primary button is "Enable").
                    HandleContentDialog(window, "Enable");
                }
                else if (!enabled && currentlyChecked)
                {
                    InvokeElement(check);
                    Thread.Sleep(SettleDelayMs + ObservationDelayMs);
                }

                if (WaitForCheckBoxState(window, "ShowSensitiveCheck", enabled, 5000))
                {
                    Console.WriteLine("Show sensitive set; pausing for observation...");
                    Thread.Sleep(ObservationDelayMs);
                    Console.WriteLine("OK");
                    return 0;
                }
                Console.WriteLine($"Show sensitive state did not settle after attempt {attempt + 1}; retrying...");
            }

            Console.Error.WriteLine($"ERROR: Show sensitive did not reach {(enabled ? "enabled" : "disabled")} after 3 attempts.");
            return 1;
        }

        private static bool WaitForCheckBoxState(Window window, string automationId, bool expectedChecked, int timeoutMs)
        {
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            do
            {
                try
                {
                    var check = FindByAutomationId(window, automationId)?.AsCheckBox();
                    if (check != null)
                    {
                        bool isChecked = check.IsChecked.HasValue && check.IsChecked.Value;
                        if (isChecked == expectedChecked)
                            return true;
                    }
                }
                catch { }
                Thread.Sleep(250);
            } while (DateTime.UtcNow < deadline);
            return false;
        }

        private static int GetIsEnabled(UIA3Automation automation, string[] args)
        {
            string id = GetArg(args, "--id");
            if (string.IsNullOrEmpty(id))
            {
                Console.Error.WriteLine("ERROR: --id is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);
            var element = FindByAutomationId(window, id);
            if (element == null)
            {
                throw new InvalidOperationException($"Control '{id}' not found.");
            }
            Console.WriteLine($"ENABLED:{element.IsEnabled}");
            bool isChecked = false;
            if (element.ControlType == FlaUI.Core.Definitions.ControlType.CheckBox)
            {
                var check = element.AsCheckBox();
                isChecked = check.IsChecked.HasValue && check.IsChecked.Value;
            }
            Console.WriteLine($"CHECKED:{isChecked}");
            return 0;
        }

        private static int AddProfile(UIA3Automation automation, string[] args)
        {
            string alias = GetArg(args, "--alias");
            if (string.IsNullOrEmpty(alias))
            {
                Console.Error.WriteLine("ERROR: --alias is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var addBtn = FindByAutomationId(window, "AddProfileBtn").AsButton();
            InvokeElement(addBtn);
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);

            // Rename the newly created profile.
            var nameBox = FindByAutomationId(window, "ProfileNameBox").AsTextBox();
            SetTextBoxValue(nameBox, alias);

            Console.WriteLine("Profile added; pausing for observation...");
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int SelectProfile(UIA3Automation automation, string[] args)
        {
            string alias = GetArg(args, "--alias");
            if (string.IsNullOrEmpty(alias))
            {
                Console.Error.WriteLine("ERROR: --alias is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var list = FindByAutomationId(window, "ProfileList").AsListBox();
            var item = list.FindFirstDescendant(x => x.ByName(alias));
            if (item == null)
            {
                throw new InvalidOperationException($"Profile '{alias}' not found in profile list.");
            }
            if (item.Patterns.SelectionItem.IsSupported)
            {
                item.Patterns.SelectionItem.Pattern.Select();
            }
            else
            {
                item.Click();
            }
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);

            Console.WriteLine("Profile selected; pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int GetProfiles(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var list = FindByAutomationId(window, "ProfileList").AsListBox();
            var items = list.FindAllChildren();
            Console.WriteLine($"COUNT:{items.Length}");
            foreach (var item in items)
            {
                Console.WriteLine(item.Name);
            }
            return 0;
        }

        private static int RemoveProfile(UIA3Automation automation, string[] args)
        {
            string alias = GetArg(args, "--alias");
            if (string.IsNullOrEmpty(alias))
            {
                Console.Error.WriteLine("ERROR: --alias is required.");
                return 1;
            }

            var window = PrepareWindow(automation);
            var list = FindByAutomationId(window, "ProfileList").AsListBox();
            var item = list.FindFirstDescendant(x => x.ByName(alias));
            if (item == null)
            {
                throw new InvalidOperationException($"Profile '{alias}' not found in profile list.");
            }
            if (item.Patterns.SelectionItem.IsSupported)
            {
                item.Patterns.SelectionItem.Pattern.Select();
            }
            else
            {
                item.Click();
            }
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);

            var removeBtn = FindByAutomationId(window, "RemoveProfileBtn")?.AsButton();
            if (removeBtn == null)
            {
                throw new InvalidOperationException("Remove profile button not found.");
            }
            InvokeElement(removeBtn);
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);
            HandleContentDialog(window, "Remove");
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int ClearStatistics(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var btn = FindByAutomationId(window, "ClearStatisticsBtn")?.AsButton();
            if (btn == null)
            {
                throw new InvalidOperationException("Clear statistics button not found.");
            }
            InvokeElement(btn);
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        private static int ClearSessionRedactions(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var btn = FindByAutomationId(window, "ClearMatchesBtn")?.AsButton();
            if (btn == null)
            {
                throw new InvalidOperationException("Clear session redactions button not found.");
            }
            InvokeElement(btn);
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        private static int ClearLogs(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);
            var btn = FindByAutomationId(window, "ClearLogsBtn")?.AsButton();
            if (btn == null)
            {
                throw new InvalidOperationException("Clear logs button not found.");
            }
            InvokeElement(btn);
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);
            HandleContentDialog(window, "Delete all logs");
            Thread.Sleep(SettleDelayMs + ObservationDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        private static int SetPiiType(UIA3Automation automation, string[] args)
        {
            string type = GetArg(args, "--type");
            bool enabled = false;
            ParseArgs(args, out type, ref enabled, "--type", "--enabled");

            if (string.IsNullOrEmpty(type))
            {
                Console.Error.WriteLine("ERROR: --type is required.");
                return 1;
            }

            // Map internal PII type names to the English UI labels used in HomePage.xaml.cs.
            var labelMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                { "account_number", "Account number" },
                { "private_address", "Address" },
                { "private_date", "Date" },
                { "private_email", "Email" },
                { "private_person", "Person" },
                { "private_phone", "Phone" },
                { "private_url", "URL" },
                { "secret", "Secret" },
            };

            if (!labelMap.TryGetValue(type, out string label))
            {
                throw new InvalidOperationException($"Unknown PII type '{type}'.");
            }

            var window = PrepareWindow(automation);
            MaximizeWindow(window);
            var cb = Retry.WhileNull(() =>
                window.FindFirstDescendant(x => x.ByName(label))?.AsCheckBox(),
                ControlFindTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
            if (cb == null)
            {
                throw new InvalidOperationException($"PII checkbox for '{label}' not found.");
            }

            bool currentlyChecked = cb.IsChecked.HasValue && cb.IsChecked.Value;
            Console.WriteLine($"PII '{label}' current state: {(currentlyChecked ? "checked" : "unchecked")}, desired: {(enabled ? "checked" : "unchecked")}");
            if (enabled != currentlyChecked)
            {
                // Use the Toggle pattern instead of a mouse click; this avoids
                // NoClickablePointException when the checkbox is virtualized.
                Console.WriteLine($"Toggling PII '{label}' via TogglePattern");
                cb.Patterns.Toggle.Pattern.Toggle();
                Thread.Sleep(ToggleDelayMs + ObservationDelayMs);
                bool newChecked = cb.IsChecked.HasValue && cb.IsChecked.Value;
                Console.WriteLine($"PII '{label}' new state: {(newChecked ? "checked" : "unchecked")}");
            }

            Console.WriteLine("PII type toggled; pausing for observation...");
            Thread.Sleep(ObservationDelayMs);

            Console.WriteLine("OK");
            return 0;
        }

        private static int GetPiiMasterState(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var toggle = FindByAutomationId(window, "UseOpenAISwitch");
            if (toggle == null)
            {
                throw new InvalidOperationException("PII master switch (UseOpenAISwitch) not found.");
            }
            var state = toggle.Patterns.Toggle.Pattern.ToggleState;
            bool isOn = state == ToggleState.On;
            Console.WriteLine(isOn ? "True" : "False");
            return 0;
        }

        private static int SetPiiMaster(UIA3Automation automation, string[] args)
        {
            bool enabled = false;
            bool.TryParse(GetArg(args, "--enabled"), out enabled);

            var window = PrepareWindow(automation);
            var toggle = FindByAutomationId(window, "UseOpenAISwitch");
            if (toggle == null)
            {
                throw new InvalidOperationException("PII master switch (UseOpenAISwitch) not found.");
            }

            var state = toggle.Patterns.Toggle.Pattern.ToggleState;
            bool currentlyOn = state == ToggleState.On;
            Console.WriteLine($"PII master switch current state: {(currentlyOn ? "On" : "Off")}, desired: {(enabled ? "On" : "Off")}");
            if (enabled != currentlyOn)
            {
                toggle.Patterns.Toggle.Pattern.Toggle();
                Thread.Sleep(ToggleDelayMs + ObservationDelayMs);
                state = toggle.Patterns.Toggle.Pattern.ToggleState;
                currentlyOn = state == ToggleState.On;
                Console.WriteLine($"PII master switch new state: {(currentlyOn ? "On" : "Off")}");
            }

            Console.WriteLine("OK");
            return 0;
        }

        private static int GetStatistics(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var statsBlock = FindByAutomationId(window, "StatsBlock").AsLabel();
            string text = statsBlock.Text ?? "";
            Console.WriteLine(text);
            return 0;
        }

        private static int GetSessionRedactions(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var list = FindByAutomationId(window, "MatchesList").AsListBox();
            var items = list.FindAllChildren();
            Console.WriteLine($"COUNT:{items.Length}");
            foreach (var item in items)
            {
                Console.WriteLine(item.Name);
            }
            return 0;
        }

        private static int Quit()
        {
            var process = Process.GetProcessesByName(ProcessName).FirstOrDefault();
            if (process != null)
            {
                try
                {
                    process.Kill();
                    process.WaitForExit(10000);
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine("WARNING: failed to kill process: " + ex.Message);
                }
            }
            return 0;
        }

        // -------------------------------------------------------------------------
        // Port / API key / master password helpers
        // -------------------------------------------------------------------------

        private static AutomationElement FindWindowButtonByName(Window window, string name, TimeSpan? timeout = null)
        {
            var effectiveTimeout = timeout ?? TimeSpan.FromSeconds(5);
            return Retry.WhileNull(() =>
                window.FindFirstDescendant(x => x.ByName(name)),
                effectiveTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static AutomationElement FindWindowEditByName(Window window, string name, TimeSpan? timeout = null)
        {
            var effectiveTimeout = timeout ?? TimeSpan.FromSeconds(5);
            return Retry.WhileNull(() =>
                window.FindFirstDescendant(x => x.ByControlType(ControlType.Edit).And(x.ByName(name))),
                effectiveTimeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static void SetEditValue(AutomationElement edit, string value)
        {
            edit.Focus();
            Thread.Sleep(50);
            using (Keyboard.Pressing(VirtualKeyShort.CONTROL))
            {
                Keyboard.Press(VirtualKeyShort.KEY_A);
            }
            Thread.Sleep(TypeDelayMs);
            Keyboard.Type(value);
            Thread.Sleep(TypeDelayMs);
        }

        private static string GetTextBlockText(AutomationElement element)
        {
            if (element == null) return "";
            try
            {
                var textPattern = element.Patterns.Text;
                if (textPattern.IsSupported)
                {
                    string t = textPattern.Pattern.DocumentRange.GetText(-1) ?? "";
                    if (!string.IsNullOrEmpty(t)) return t;
                }
            }
            catch { }
            try
            {
                return element.Properties.Name.ValueOrDefault ?? "";
            }
            catch { }
            return "";
        }

        private static string GetEditValue(AutomationElement element)
        {
            if (element == null) return "";
            try
            {
                var valuePattern = element.Patterns.Value;
                if (valuePattern.IsSupported)
                    return valuePattern.Pattern.Value ?? "";
            }
            catch { }
            try
            {
                return element.AsTextBox().Text ?? "";
            }
            catch { }
            return "";
        }

        private static int GetProxyStatus(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var statusBlock = FindByAutomationId(window, "ProxyStatusBlock");
            string text = GetTextBlockText(statusBlock);
            Console.WriteLine($"TEXT:{text}");
            return 0;
        }

        private static int GetProfileDetails(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var list = FindByAutomationId(window, "ProfileList").AsListBox();
            var items = list.FindAllChildren();
            string originalSelection = null;
            foreach (var item in items)
            {
                if (item.Patterns.SelectionItem.IsSupported &&
                    item.Patterns.SelectionItem.Pattern.IsSelected)
                {
                    originalSelection = item.Name;
                    break;
                }
            }

            Console.WriteLine($"COUNT:{items.Length}");
            foreach (var item in items)
            {
                if (item.Patterns.SelectionItem.IsSupported)
                    item.Patterns.SelectionItem.Pattern.Select();
                else
                    item.Click();
                Thread.Sleep(SettleDelayMs);

                var nameBox = FindByAutomationId(window, "ProfileNameBox").AsTextBox();
                var portBox = FindByAutomationId(window, "PortBox").AsTextBox();
                Console.WriteLine($"ALIAS:{nameBox.Text}|PORT:{portBox.Text}");
            }

            if (!string.IsNullOrEmpty(originalSelection))
            {
                var restore = list.FindFirstDescendant(x => x.ByName(originalSelection));
                restore?.Patterns.SelectionItem.Pattern.Select();
            }

            return 0;
        }

        private static int GetApiKeyVisibility(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var keyBox = FindByAutomationId(window, "ApiKeyBox");
            var showCheck = FindByAutomationId(window, "ShowKeyCheck").AsCheckBox();
            bool showChecked = showCheck.IsChecked.HasValue && showCheck.IsChecked.Value;
            string visibleText = GetEditValue(keyBox);
            Console.WriteLine($"CHECKED:{showChecked}");
            Console.WriteLine($"TEXT:{visibleText}");
            return 0;
        }

        private static int ToggleShowApiKey(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            var showCheck = FindByAutomationId(window, "ShowKeyCheck").AsCheckBox();
            InvokeElement(showCheck);
            Thread.Sleep(SettleDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        private static int GetChangePasswordButtonState(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);
            var btn = FindByAutomationId(window, "ChangePasswordBtn").AsButton();
            Console.WriteLine($"ENABLED:{btn.IsEnabled}");
            return 0;
        }

        private static int SetMasterPassword(UIA3Automation automation, string[] args)
        {
            bool enabled = false;
            if (!bool.TryParse(GetArg(args, "--enabled"), out enabled))
            {
                Console.Error.WriteLine("ERROR: --enabled is required (true|false).");
                return 1;
            }
            string password = GetArg(args, "--password") ?? "";
            string confirm = GetArg(args, "--confirm") ?? password;

            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);

            bool tookAction = false;
            for (int attempt = 0; attempt < 3; attempt++)
            {
                // The app reverts RequirePasswordCheck while the dialog is open
                // (HomePage.cpp RequirePassword_Click), so the checkbox state is
                // not a reliable progress signal; check the Change password
                // button, which is enabled exactly when a master password is set.
                if (WaitForChangePasswordButtonState(window, enabled, attempt == 0 ? 1000 : 500))
                {
                    if (!tookAction)
                        Console.WriteLine($"Master password already {(enabled ? "enabled" : "disabled")}.");
                    Console.WriteLine("OK");
                    return 0;
                }

                // Reuse a dialog left open by a previous attempt instead of
                // toggling the checkbox again.
                string editName = enabled ? "Password" : "Current password";
                string buttonName = enabled ? "Set Password" : "Disable";
                var passEdit = FindDialogEditByName(automation, window, editName, TimeSpan.FromSeconds(1));
                if (passEdit == null)
                {
                    var check = FindByAutomationId(window, "RequirePasswordCheck").AsCheckBox();
                    InvokeElement(check);

                    // Poll for the dialog's password boxes; on slow runners the
                    // ContentDialog can take many seconds to appear and may
                    // render as a popup outside the main window.
                    passEdit = FindDialogEditByName(automation, window, editName, TimeSpan.FromSeconds(30));
                }
                tookAction = true;

                if (enabled)
                {
                    var confirmEdit = FindDialogEditByName(automation, window, "Confirm password", TimeSpan.FromSeconds(10));
                    if (passEdit == null || confirmEdit == null)
                    {
                        DumpUiTree(automation, window);
                        throw new InvalidOperationException("Set master password password boxes not found.");
                    }
                    SetEditValue(passEdit, password);
                    SetEditValue(confirmEdit, confirm);
                }
                else
                {
                    if (passEdit == null)
                    {
                        DumpUiTree(automation, window);
                        throw new InvalidOperationException("Disable master password password box not found.");
                    }
                    SetEditValue(passEdit, password);
                }

                var btn = FindDialogButtonByName(automation, window, buttonName, TimeSpan.FromSeconds(10));
                if (btn == null)
                    throw new InvalidOperationException($"Master password dialog button '{buttonName}' not found.");
                InvokeElement(btn);

                Thread.Sleep(SettleDelayMs);

                if (WaitForChangePasswordButtonState(window, enabled, 5000))
                {
                    Console.WriteLine("OK");
                    return 0;
                }
                Console.WriteLine($"Master password state did not settle after attempt {attempt + 1}; retrying...");
            }

            Console.Error.WriteLine($"ERROR: Change password button did not become {(enabled ? "enabled" : "disabled")} after 3 attempts.");
            return 1;
        }

        private static AutomationElement FindDialogEditByName(UIA3Automation automation, Window window, string name, TimeSpan timeout)
        {
            return FindDialogElement(automation, window, name, true, timeout);
        }

        private static AutomationElement FindDialogButtonByName(UIA3Automation automation, Window window, string name, TimeSpan timeout)
        {
            return FindDialogElement(automation, window, name, false, timeout);
        }

        private static AutomationElement FindDialogElement(UIA3Automation automation, Window window, string name, bool editsOnly, TimeSpan timeout)
        {
            // ContentDialogs may render as a popup that is not a descendant of
            // the main window, so after the window search, scan the desktop's
            // top-level windows one by one. Every UIA call is wrapped so a
            // single misbehaving element or window (e.g. COMException
            // E_UNEXPECTED) is treated as "not found, keep polling" instead of
            // aborting the whole search.
            var desktop = automation.GetDesktop();
            return Retry.WhileNull(() =>
            {
                try
                {
                    var el = editsOnly
                        ? window.FindFirstDescendant(x => x.ByControlType(ControlType.Edit).And(x.ByName(name)))
                        : window.FindFirstDescendant(x => x.ByName(name));
                    if (el != null) return el;
                }
                catch { }

                AutomationElement[] topLevel;
                try { topLevel = desktop.FindAllChildren(); }
                catch { return null; }

                foreach (var top in topLevel)
                {
                    try
                    {
                        var el = editsOnly
                            ? top.FindFirstDescendant(x => x.ByControlType(ControlType.Edit).And(x.ByName(name)))
                            : top.FindFirstDescendant(x => x.ByName(name));
                        if (el != null) return el;
                    }
                    catch { }
                }
                return null;
            }, timeout, TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
        }

        private static void DumpUiTree(UIA3Automation automation, Window window)
        {
            // Diagnostic dump for CI logs: what is actually on screen when the
            // master password dialog cannot be found. Prints structure only
            // (control types, names, ids) — never field values, so no password
            // content can leak.
            const int maxLines = 300;
            int lines = 0;

            try
            {
                var check = FindByAutomationId(window, "RequirePasswordCheck")?.AsCheckBox();
                var btn = FindByAutomationId(window, "ChangePasswordBtn")?.AsButton();
                Console.WriteLine($"DIAG RequirePasswordCheck.IsChecked={check?.IsChecked} ChangePasswordBtn.IsEnabled={btn?.IsEnabled}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"DIAG state read failed: {ex.Message}");
            }

            AutomationElement[] topLevel;
            try { topLevel = automation.GetDesktop().FindAllChildren(); }
            catch (Exception ex)
            {
                Console.WriteLine($"DIAG desktop enumeration failed: {ex.Message}");
                return;
            }

            Console.WriteLine($"DIAG top-level windows: {topLevel.Length}");
            foreach (var top in topLevel)
            {
                if (lines >= maxLines)
                {
                    Console.WriteLine("DIAG ... output truncated ...");
                    return;
                }

                string name, className, automationId;
                try
                {
                    name = top.Properties.Name.ValueOrDefault ?? "";
                    className = top.Properties.ClassName.ValueOrDefault ?? "";
                    automationId = top.Properties.AutomationId.ValueOrDefault ?? "";
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"DIAG window <unreadable: {ex.Message}>");
                    lines++;
                    continue;
                }

                Console.WriteLine($"DIAG window Name='{name}' Class='{className}' Id='{automationId}'");
                lines++;

                string haystack = name + " " + className + " " + automationId;
                bool interesting =
                    haystack.IndexOf("Agent", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    haystack.IndexOf("Redactor", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    haystack.IndexOf("Xaml", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    haystack.IndexOf("Popup", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    haystack.IndexOf("Dialog", StringComparison.OrdinalIgnoreCase) >= 0;
                if (interesting)
                {
                    DumpDescendants(top, 1, 3, ref lines, maxLines);
                }
            }
        }

        private static void DumpDescendants(AutomationElement parent, int depth, int maxDepth, ref int lines, int maxLines)
        {
            if (depth > maxDepth || lines >= maxLines) return;

            AutomationElement[] children;
            try { children = parent.FindAllChildren(); }
            catch { return; }

            foreach (var child in children)
            {
                if (lines >= maxLines) return;
                try
                {
                    string indent = new string(' ', depth * 2);
                    string childName = child.Properties.Name.ValueOrDefault ?? "";
                    string childId = child.Properties.AutomationId.ValueOrDefault ?? "";
                    bool offscreen = child.Properties.IsOffscreen.ValueOrDefault;
                    Console.WriteLine($"DIAG {indent}{child.ControlType} Name='{childName}' Id='{childId}' Offscreen={offscreen}");
                    lines++;
                }
                catch { continue; }
                DumpDescendants(child, depth + 1, maxDepth, ref lines, maxLines);
            }
        }

        private static bool WaitForChangePasswordButtonState(Window window, bool expectedEnabled, int timeoutMs)
        {
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            do
            {
                try
                {
                    var btn = FindByAutomationId(window, "ChangePasswordBtn");
                    if (btn != null && btn.AsButton().IsEnabled == expectedEnabled)
                        return true;
                }
                catch { }
                Thread.Sleep(250);
            } while (DateTime.UtcNow < deadline);
            return false;
        }

        private static int ChangeMasterPassword(UIA3Automation automation, string[] args)
        {
            string oldPass = GetArg(args, "--old") ?? "";
            string newPass = GetArg(args, "--new") ?? "";
            string confirm = GetArg(args, "--confirm") ?? newPass;

            var window = PrepareWindow(automation);
            ScrollToBottomWithKeyboard(window);
            var btn = FindByAutomationId(window, "ChangePasswordBtn").AsButton();
            if (!btn.IsEnabled)
                throw new InvalidOperationException("Change password button is disabled; master password may not be enabled.");
            InvokeElement(btn);
            Thread.Sleep(SettleDelayMs);

            var oldEdit = FindWindowEditByName(window, "Current password", TimeSpan.FromSeconds(10));
            var newEdit = FindWindowEditByName(window, "New password", TimeSpan.FromSeconds(10));
            var confirmEdit = FindWindowEditByName(window, "Confirm new password", TimeSpan.FromSeconds(10));
            if (oldEdit == null || newEdit == null || confirmEdit == null)
                throw new InvalidOperationException("Change master password password boxes not found.");
            SetEditValue(oldEdit, oldPass);
            SetEditValue(newEdit, newPass);
            SetEditValue(confirmEdit, confirm);
            var changeBtn = FindWindowButtonByName(window, "Change", TimeSpan.FromSeconds(5));
            InvokeElement(changeBtn);

            Thread.Sleep(SettleDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        private static int UnlockMasterPassword(UIA3Automation automation, string[] args)
        {
            string password = GetArg(args, "--password") ?? "";

            // Wait for the startup dialog to appear.
            FindAgentRedactorProcess();
            Thread.Sleep(500);

            // WinUI 3 ContentDialogs may render in a popup that is not a direct
            // descendant of the main window. Search the desktop for the Unlock
            // button so we can handle the startup password dialog regardless.
            var desktop = automation.GetDesktop();
            AutomationElement unlockBtn = Retry.WhileNull(() =>
                desktop.FindFirstDescendant(x => x.ByName("Unlock")),
                TimeSpan.FromSeconds(15), TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
            if (unlockBtn == null)
                throw new InvalidOperationException("Master password unlock button not found on desktop.");

            // Walk up to the dialog root to scope the edit search.
            AutomationElement scope = unlockBtn;
            while (scope != null && scope.ControlType != ControlType.Window && scope.ControlType != ControlType.Pane)
            {
                try { scope = scope.Parent; }
                catch { break; }
            }
            if (scope == null) scope = desktop;

            var passEdit = Retry.WhileNull(() =>
                scope.FindFirstDescendant(x => x.ByControlType(ControlType.Edit)),
                TimeSpan.FromSeconds(5), TimeSpan.FromMilliseconds(RetryPollIntervalMs)).Result;
            if (passEdit == null)
                throw new InvalidOperationException("Master password unlock password box not found.");

            SetEditValue(passEdit, password);
            InvokeElement(unlockBtn);

            Thread.Sleep(SettleDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        private static int GetContentDialogText(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);

            // ContentDialogs in WinUI 3 are rendered as popup siblings. Collect all
            // text blocks in the window and return the ones that look like dialog text.
            var textBlocks = window.FindAllDescendants(x => x.ByControlType(ControlType.Text));
            bool foundAny = false;
            foreach (var tb in textBlocks)
            {
                string text = tb.Properties.Name.ValueOrDefault;
                if (!string.IsNullOrWhiteSpace(text))
                {
                    foundAny = true;
                    Console.WriteLine($"TEXT:{text}");
                }
            }
            if (!foundAny)
                Console.WriteLine("NO_DIALOG");
            return 0;
        }

        private static int DismissContentDialog(UIA3Automation automation)
        {
            var window = PrepareWindow(automation);

            AutomationElement closeBtn = null;
            // Most validation/confirmation error dialogs use a close button named "OK".
            try { closeBtn = window.FindFirstDescendant(x => x.ByName("OK")); } catch { }
            if (closeBtn == null)
                try { closeBtn = window.FindFirstDescendant(x => x.ByAutomationId("CloseButton")); } catch { }
            if (closeBtn == null)
                try { closeBtn = window.FindFirstDescendant(x => x.ByAutomationId("PrimaryButton")); } catch { }

            if (closeBtn != null)
                InvokeElement(closeBtn);
            else
                throw new InvalidOperationException("Could not find a button to dismiss the content dialog.");

            Thread.Sleep(SettleDelayMs);
            Console.WriteLine("OK");
            return 0;
        }

        // --- Shared helpers ---

        private static Window PrepareWindow(UIA3Automation automation)
        {
            var window = GetMainWindow(automation);
            window.WaitUntilClickable(TimeSpan.FromSeconds(5));
            BringToForeground(window);
            MaximizeWindow(window);
            Thread.Sleep(WindowPrepareDelayMs);
            return window;
        }

        private static void SetCaseSensitive(Window window, bool caseSensitive)
        {
            var caseCheckElement = FindByAutomationId(window, "CaseSensitiveCheck");
            if (caseCheckElement != null)
            {
                var caseCheck = caseCheckElement.AsCheckBox();
                bool currentlyChecked = caseCheck.IsChecked.HasValue && caseCheck.IsChecked.Value;
                if (caseSensitive != currentlyChecked)
                {
                    InvokeElement(caseCheck);
                    Thread.Sleep(50);
                }
            }
        }

        private static string GetArg(string[] args, string name)
        {
            for (int i = 1; i < args.Length; i++)
            {
                if (args[i] == name && i + 1 < args.Length)
                {
                    return args[++i];
                }
            }
            return null;
        }

        private static void ParseArgs(string[] args, out string text, ref bool flag, string textArg, string flagArg)
        {
            text = null;
            for (int i = 1; i < args.Length; i++)
            {
                if (args[i] == textArg && i + 1 < args.Length)
                {
                    text = args[++i];
                }
                else if (args[i] == flagArg && i + 1 < args.Length)
                {
                    bool.TryParse(args[++i], out flag);
                }
            }
        }
    }
}
