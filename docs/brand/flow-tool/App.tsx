import { useState } from 'react';
import { Flow } from 'flow-sdk';
import { ResultCard } from './components/ResultCard';
import { Icon, IconButton, PillButton } from './components/Primitives';
import { SettingsPanel } from './components/SettingsPanel';
import { FEATURE_LABELS, IMAGE_MODELS, VIDEO_MODELS } from './constants';
import { downloadMedia, errorMessage, extensionFor, slugify } from './lib/flowUtils';
import { useGlobalStyles } from './lib/globalStyles';
import { isVideoKind, makeJob, runJob } from './lib/jobs';
import { buildPrompt, taskLabel } from './lib/prompts';
import { downloadZip } from './lib/zip';
import type { MediaResult, ProjectMedia, Settings, Task } from './types';

type FinishedTask = Task & { result: MediaResult };

const isFinished = (task: Task): task is FinishedTask => Boolean(task.result);

const DEFAULT_SETTINGS: Settings = {
  kind: 'Hero loop',
  feature: FEATURE_LABELS[0],
  extra: '',
  imageModel: IMAGE_MODELS[0],
  videoModel: VIDEO_MODELS[0],
  imageAspect: '16:9',
  videoAspect: '16:9',
  duration: '8',
  count: '2',
};

export default function App() {
  useGlobalStyles();

  const [settings, setSettings] = useState<Settings>(DEFAULT_SETTINGS);
  const [logo, setLogo] = useState<ProjectMedia | null>(null);
  const [tasks, setTasks] = useState<Task[]>([]);
  const [isGenerating, setIsGenerating] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  const prompt = buildPrompt(settings);
  const runs = isVideoKind(settings.kind) ? 1 : Number(settings.count);
  const runLabel = isVideoKind(settings.kind) ? 'Generate 1 video' : `Generate ${runs} image${runs === 1 ? '' : 's'}`;
  const finishedTasks = tasks.filter(isFinished);

  const updateSettings = (patch: Partial<Settings>) => setSettings(previous => ({ ...previous, ...patch }));

  const updateTask = (id: string, patch: Partial<Task>) => {
    setTasks(previous => previous.map(task => (task.id === id ? { ...task, ...patch } : task)));
  };

  const chooseLogo = async () => {
    try {
      const media = await Flow.media.select({ filter: 'image' });
      if (media) setLogo(media);
    } catch (error) {
      setNotice(errorMessage(error));
    }
  };

  const runTask = async (task: Task) => {
    updateTask(task.id, { status: 'generating', error: undefined });
    try {
      const result = await runJob(task.job);
      updateTask(task.id, { status: 'done', result });
    } catch (error) {
      updateTask(task.id, { status: 'error', error: errorMessage(error) });
    }
  };

  const generate = async () => {
    if (isGenerating) return;
    setNotice(null);
    setIsGenerating(true);
    try {
      const job = await makeJob(settings, prompt, logo?.mediaId);
      const label = taskLabel(settings);
      const batch: Task[] = Array.from({ length: runs }, (_, index) => ({
        id: `${Date.now()}-${index}`,
        label,
        job,
        status: 'generating',
      }));
      setTasks(previous => [...batch, ...previous]);
      for (const task of batch) await runTask(task); // one request at a time
    } catch (error) {
      setNotice(errorMessage(error));
    } finally {
      setIsGenerating(false);
    }
  };

  const downloadTask = async (task: Task) => {
    if (!task.result) return;
    try {
      await downloadMedia(task.result, `beatrealms_${task.label}_${task.id}`);
    } catch (error) {
      setNotice(errorMessage(error));
    }
  };

  const exportAll = async () => {
    const files = finishedTasks.map((task, index) => ({
      name: `${String(index + 1).padStart(2, '0')}_${slugify(task.label)}.${extensionFor(task.result.mimeType)}`,
      base64: task.result.base64,
    }));
    try {
      await downloadZip(files, 'beatrealms_flow_assets.zip');
    } catch (error) {
      setNotice(errorMessage(error));
    }
  };

  return (
    <div className="flex h-screen w-screen overflow-hidden bg-[#0e0e0e] text-white">
      <aside className="flex h-full w-[300px] shrink-0 flex-col justify-between gap-4 border-r border-[rgba(218,220,224,0.15)] px-[10px] py-[12px]">
        <div className="dark-scrollbar flex min-h-0 flex-col gap-6 overflow-y-auto">
          <SettingsPanel
            settings={settings}
            onChange={updateSettings}
            logo={logo}
            onChooseLogo={chooseLogo}
            onClearLogo={() => setLogo(null)}
            prompt={prompt}
            disabled={isGenerating}
          />
          {notice && <p className="px-2 text-[11px] font-medium text-red-400/80">{notice}</p>}
        </div>

        <div className="flex flex-col gap-2 border-t border-white/5 pt-4">
          <PillButton variant="solid" icon={<Icon name="arrow_forward" />} onClick={generate} disabled={isGenerating}>
            {isGenerating ? 'Generating…' : runLabel}
          </PillButton>
          <div className="flex gap-2">
            <PillButton variant="outline" icon={<Icon name="download" />} onClick={exportAll} disabled={finishedTasks.length === 0} className="flex-1">
              Export all
            </PillButton>
            <IconButton icon="refresh" label="Clear results" onClick={() => setTasks([])} disabled={isGenerating} />
          </div>
        </div>
      </aside>

      <main className="relative min-w-0 flex-1">
        <div className="dark-scrollbar absolute inset-0 overflow-y-auto p-8">
          {tasks.length === 0 ? (
            <div className="pointer-events-none flex h-full flex-col items-center justify-center opacity-10">
              <Icon name="visibility" size={120} />
              <h2 className="mt-4 text-2xl font-light uppercase tracking-[8px]">BeatRealms Studio</h2>
            </div>
          ) : (
            <div className="mx-auto grid max-w-[1600px] grid-cols-1 gap-6 pb-12 sm:grid-cols-2 lg:grid-cols-3">
              {tasks.map(task => (
                <ResultCard key={task.id} task={task} onDownload={() => downloadTask(task)} onRetry={() => runTask(task)} />
              ))}
            </div>
          )}
        </div>
      </main>
    </div>
  );
}
