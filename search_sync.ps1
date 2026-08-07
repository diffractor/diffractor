$patterns = @(
    "std::atomic", "platform::mutex", "std::mutex", "thread_event", "task_queue", 
    "platform::queue", "platform::threads", "std::thread", "CRITICAL_SECTION", "SRWLOCK", 
    "Interlocked", "_Guarded_by_"
)
$regex = $patterns -join "|"
$output = foreach ($file in (Get-ChildItem -Path "src\*.h", "src\*.cpp" -File)) {
    $matches = Get-Content $file.FullName | Select-String -Pattern $regex -SimpleMatch:$false
    if ($matches) {
        "[File: $($file.Name)]"
        foreach ($m in $matches) {
            "  Line $($m.LineNumber): $($m.Line.Trim())"
        }
    }
}
$output | Out-File "matches.txt" -Encoding utf8
