import { useEffect } from 'react';

const STYLE_ID = 'flow-tool-global-styles';

const GLOBAL_CSS = `
  html, body, #root { margin: 0; padding: 0; width: 100%; height: 100%; background: #0e0e0e; color: white; overflow: hidden; font-family: 'Google Sans Text', sans-serif; }
  .dark-scrollbar { scrollbar-width: thin; scrollbar-color: #595959 transparent; }
  .dark-scrollbar::-webkit-scrollbar { width: 6px; }
  .dark-scrollbar::-webkit-scrollbar-track { background: transparent; }
  .dark-scrollbar::-webkit-scrollbar-thumb { background: #595959; border-radius: 9999px; }
  @keyframes fade-in { from { opacity: 0; transform: translateY(10px); } to { opacity: 1; transform: translateY(0); } }
  .animate-fade-in { animation: fade-in 0.4s ease-out forwards; }
`;

export function useGlobalStyles(): void {
  useEffect(() => {
    if (document.getElementById(STYLE_ID)) return;
    const style = document.createElement('style');
    style.id = STYLE_ID;
    style.textContent = GLOBAL_CSS;
    document.head.appendChild(style);
  }, []);
}
