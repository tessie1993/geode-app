import type {
  AssetKind,
  FeatureLabel,
  ImageAspectRatio,
  ImageCount,
  ImageModel,
  VideoAspectRatio,
  VideoDuration,
  VideoModel,
} from './constants';

export type TaskStatus = 'generating' | 'done' | 'error';

export interface MediaResult {
  mediaId: string;
  base64: string;
  mimeType: string;
}

export interface ProjectMedia extends MediaResult {
  type: 'image' | 'video' | 'audio';
  name: string;
}

export interface Settings {
  kind: AssetKind;
  feature: FeatureLabel;
  extra: string;
  imageModel: ImageModel;
  videoModel: VideoModel;
  imageAspect: ImageAspectRatio;
  videoAspect: VideoAspectRatio;
  duration: VideoDuration;
  count: ImageCount;
}

export interface ImageJob {
  type: 'image';
  prompt: string;
  model: ImageModel;
  aspectRatio: ImageAspectRatio;
  referenceMediaIds: string[];
}

export interface VideoJob {
  type: 'video';
  prompt: string;
  model: VideoModel;
  aspectRatio: VideoAspectRatio;
  durationSeconds: number;
  firstFrameMediaId: string;
  lastFrameMediaId: string;
}

export type Job = ImageJob | VideoJob;

/** Everything a run needs is on the task, so Retry repeats it exactly. */
export interface Task {
  id: string;
  label: string;
  job: Job;
  status: TaskStatus;
  result?: MediaResult;
  error?: string;
}
