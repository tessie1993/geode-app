import { toDataUrl } from '../lib/flowUtils';
import type { Task } from '../types';
import { Icon } from './Primitives';

interface ResultCardProps {
  task: Task;
  onDownload: () => void;
  onRetry: () => void;
}

export function ResultCard({ task, onDownload, onRetry }: ResultCardProps) {
  const url = task.result ? toDataUrl(task.result.base64, task.result.mimeType) : '';
  const isVideo = task.result?.mimeType.startsWith('video/') ?? task.job.type === 'video';

  return (
    <figure className="animate-fade-in m-0 flex flex-col gap-2">
      <div
        className="group relative flex items-center justify-center overflow-hidden rounded-2xl border border-[#595959] bg-[#1a1a1a] transition-colors hover:border-[#7a7a7a]"
        style={{ aspectRatio: task.job.aspectRatio.replace(':', ' / ') }}
      >
        {task.status === 'generating' && (
          <div className="flex flex-col items-center gap-4">
            <div className="h-12 w-12 animate-spin rounded-full border-4 border-[#969696]/20 border-t-[#969696]" />
            <span className="text-[11px] font-medium uppercase tracking-wider text-white/50">
              {task.job.type === 'video' ? 'Generating video…' : 'Generating…'}
            </span>
          </div>
        )}

        {task.status === 'error' && (
          <div className="flex flex-col items-center gap-2 p-4 text-center">
            <Icon name="error" size={32} className="text-red-400" />
            <span className="text-[11px] font-medium text-red-400/80">{task.error ?? 'Generation failed'}</span>
            <button type="button" onClick={onRetry} className="text-[11px] font-medium text-white/70 underline hover:text-white">
              Retry
            </button>
          </div>
        )}

        {task.status === 'done' && task.result && (
          <>
            {isVideo ? (
              <video src={url} className="h-full w-full object-cover" autoPlay muted loop playsInline controls />
            ) : (
              <img src={url} alt={task.label} className="h-full w-full object-cover" />
            )}
            <button
              type="button"
              onClick={onDownload}
              title="Download"
              aria-label="Download"
              className="absolute right-3 top-3 rounded-full bg-white p-2 text-black opacity-0 shadow-lg transition-opacity hover:bg-gray-200 focus:opacity-100 group-hover:opacity-100"
            >
              <Icon name="download" size={20} />
            </button>
          </>
        )}
      </div>
      <figcaption className="px-1 text-[11px] font-medium tracking-[0.1px] text-white/50">{task.label}</figcaption>
    </figure>
  );
}
