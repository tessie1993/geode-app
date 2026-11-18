// Type declarations transcribed from the flow-sdk source (Flow web build
// boq_labs-ai-sandbox-frontend_20260912.04_p0). Used for local type-checking only;
// Flow's compiler (esbuild) strips types and does not type-check.
declare module 'flow-sdk' {
  export type MediaType = 'image' | 'video' | 'audio';
  export type MediaFilter = MediaType | 'all';

  export interface MediaItem {
    mediaId: string;
    base64: string;
    mimeType: string;
    type: MediaType;
    name: string;
  }

  export interface GenerateResult {
    mediaId: string;
    base64: string;
    mimeType: string;
  }

  export interface InlineMedia {
    base64: string;
    mimeType?: string;
  }

  export interface TextOptions {
    systemInstruction?: string;
    thinkingLevel?: 'low' | 'medium' | 'high';
    images?: InlineMedia[];
    videos?: InlineMedia[];
    audios?: InlineMedia[];
  }

  export type StorageValue =
    | string
    | number
    | boolean
    | (StorageValue | null)[]
    | { [key: string]: StorageValue | null };

  export const Flow: {
    media: {
      select(options?: string | { id?: string; filter?: MediaFilter }): Promise<MediaItem | null>;
      selectMultiple(
        options?: number | { maxCount?: number; filter?: MediaFilter; preSelectedIds?: string[] },
      ): Promise<MediaItem[]>;
    };
    save(options: { base64: string; mimeType: string; name?: string }): Promise<{ mediaId: string }>;
    upload(options: { base64: string; mimeType: string; name?: string }): Promise<{ mediaId: string }>;
    download(options: { base64: string; mimeType: string; filename: string }): Promise<{ isSuccess: true }>;
    generate: {
      image(options: {
        prompt: string;
        modelDisplayName?: string;
        referenceImageMediaIds?: string[];
        aspectRatio?: string;
      }): Promise<GenerateResult>;
      video(options: {
        prompt: string;
        modelDisplayName?: string;
        firstFrameImageMediaId?: string;
        lastFrameImageMediaId?: string;
        referenceImageMediaIds?: string[];
        sourceVideoMediaId?: string;
        sourceVideoMode?: string;
        audioReferenceMediaIds?: string[];
        aspectRatio?: string;
        durationSeconds?: number;
        resolution?: string;
      }): Promise<GenerateResult>;
      text(prompt: string, options?: TextOptions): Promise<{ text: string }>;
      text(options: TextOptions & { prompt: string }): Promise<{ text: string }>;
    };
    camera: {
      capture(options?: { facingMode?: string }): Promise<{ base64: string; mimeType: string; width: number; height: number }>;
      stream(options?: { facingMode?: string; frameRate?: number; resolution?: string }): Promise<{
        width: number;
        height: number;
        onFrame(callback: (frame: { bitmap: ImageBitmap; width: number; height: number; timestamp: number }) => void): void;
        stop(): Promise<void>;
      }>;
    };
    microphone: {
      record(options?: { durationMs?: number }): Promise<{ base64: string; mimeType: string; durationMs: number }>;
    };
    storage: {
      getItem(key: string): Promise<StorageValue | null>;
      setItem(key: string, value: StorageValue): Promise<void>;
      removeItem(key: string): Promise<void>;
      clear(): Promise<void>;
      keys(options?: { prefix?: string }): Promise<string[]>;
    };
  };
}
