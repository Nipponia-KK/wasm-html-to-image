import React from "react";

interface EditorProps {
  code: string;
  onChangeCode: (code: string) => void;
  description?: string;
  onKeyDownRun?: () => void;
}

export const Editor: React.FC<EditorProps> = ({
  code,
  onChangeCode,
  description,
  onKeyDownRun,
}) => {
  const lineCount = code.split("\n").length;

  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    // Ctrl+Enter or Cmd+Enter to run render
    if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
      e.preventDefault();
      onKeyDownRun?.();
    }
  };

  return (
    <div className="flex flex-col h-full bg-slate-900 border-r border-slate-700/80">
      <div className="flex items-center justify-between px-3 py-2 bg-slate-800/80 border-b border-slate-700 text-xs text-slate-300">
        <span className="font-semibold text-slate-200">Mermaid Code</span>
        <div className="flex items-center gap-2 text-slate-400">
          <span>{lineCount} lines</span>
          <span>•</span>
          <span className="text-[11px] text-slate-500">Ctrl+Enter to Run</span>
        </div>
      </div>

      {description && (
        <div className="px-3 py-1.5 bg-slate-800/40 border-b border-slate-800 text-[11px] text-slate-400">
          {description}
        </div>
      )}

      <div className="relative flex-1 flex overflow-hidden font-mono text-xs">
        {/* Line Numbers */}
        <div className="select-none py-3 px-2 text-right bg-slate-950/60 text-slate-600 border-r border-slate-800 w-10 shrink-0">
          {Array.from({ length: lineCount }).map((_, i) => (
            <div key={i} className="leading-5">
              {i + 1}
            </div>
          ))}
        </div>

        {/* Text Area */}
        <textarea
          value={code}
          onChange={(e) => onChangeCode(e.target.value)}
          onKeyDown={handleKeyDown}
          spellCheck={false}
          className="flex-1 w-full h-full p-3 bg-transparent text-slate-200 resize-none outline-none leading-5 overflow-auto selection:bg-indigo-600/40"
          placeholder="Enter Mermaid diagram code..."
        />
      </div>
    </div>
  );
};
