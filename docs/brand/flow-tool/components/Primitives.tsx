import type { ReactNode } from 'react';

const LABEL_CLASS = 'text-[11px] font-medium tracking-[0.1px] text-[rgba(255,255,255,0.35)]';
const FIELD_CLASS =
  'flex flex-col gap-0.5 rounded-xl border border-[#595959] pb-2 pl-2.5 pr-2 pt-[5px] focus-within:border-[#7a7a7a]';
const VALUE_CLASS = 'bg-transparent text-[11px] font-medium tracking-[0.1px] text-white outline-none';

export function Icon({ name, size = 18, className = '' }: { name: string; size?: number; className?: string }) {
  return (
    <span aria-hidden="true" className={`material-symbols-outlined leading-none ${className}`} style={{ fontSize: size }}>
      {name}
    </span>
  );
}

export function SectionLabel({ children }: { children: ReactNode }) {
  return (
    <div className="flex items-center px-2">
      <span className="text-[11px] font-medium normal-case tracking-[0.1px] text-[rgba(218,220,224,0.9)]">{children}</span>
    </div>
  );
}

type ButtonVariant = 'filled' | 'outline' | 'solid';

const BUTTON_BASE =
  'flex h-[34px] w-full cursor-pointer select-none items-center justify-center gap-[2px] rounded-xl font-medium tracking-[0.1px] transition-all disabled:cursor-not-allowed disabled:opacity-50';

const BUTTON_VARIANTS: Record<ButtonVariant, string> = {
  filled: 'bg-[#969696] py-1 pl-[8px] pr-[24px] text-[11px] text-black hover:bg-[#a6a6a6] active:bg-[#868686]',
  outline:
    'border border-[#595959] py-2 pl-[8px] pr-[16px] text-[12px] text-white backdrop-blur-[40px] hover:bg-white/5 active:bg-white/10',
  solid: 'bg-white py-2 pl-[8px] pr-[16px] text-[12px] text-black hover:bg-gray-200 active:bg-gray-300',
};

interface PillButtonProps {
  children: ReactNode;
  icon?: ReactNode;
  variant?: ButtonVariant;
  onClick?: () => void;
  disabled?: boolean;
  className?: string;
}

export function PillButton({ children, icon, variant = 'filled', onClick, disabled, className = '' }: PillButtonProps) {
  return (
    <button
      type="button"
      className={`${BUTTON_BASE} ${BUTTON_VARIANTS[variant]} ${className}`}
      onClick={onClick}
      disabled={disabled}
    >
      {icon && <span className="flex h-6 w-6 items-center justify-center">{icon}</span>}
      <span>{children}</span>
    </button>
  );
}

interface IconButtonProps {
  icon: string;
  label: string;
  onClick?: () => void;
  disabled?: boolean;
}

export function IconButton({ icon, label, onClick, disabled }: IconButtonProps) {
  return (
    <button
      type="button"
      aria-label={label}
      title={label}
      onClick={onClick}
      disabled={disabled}
      className="flex h-[34px] w-12 shrink-0 cursor-pointer items-center justify-center rounded-xl border border-[#595959] text-white transition-all hover:bg-white/5 active:bg-white/10 disabled:cursor-not-allowed disabled:opacity-50"
    >
      <Icon name={icon} />
    </button>
  );
}

interface TextAreaFieldProps {
  label: string;
  value: string;
  onChange: (value: string) => void;
  placeholder?: string;
  rows?: number;
  maxLength?: number;
  disabled?: boolean;
}

export function TextAreaField({ label, value, onChange, placeholder, rows = 3, maxLength, disabled }: TextAreaFieldProps) {
  return (
    <label className={FIELD_CLASS}>
      <span className={LABEL_CLASS}>{label}</span>
      <textarea
        className={`${VALUE_CLASS} dark-scrollbar resize-none placeholder:text-white/30`}
        value={value}
        rows={rows}
        maxLength={maxLength}
        placeholder={placeholder}
        disabled={disabled}
        onChange={event => onChange(event.target.value)}
      />
    </label>
  );
}

interface ChoiceProps<T extends string> {
  label: string;
  value: T;
  options: readonly T[];
  onChange: (value: T) => void;
  disabled?: boolean;
}

export function SelectField<T extends string>({ label, value, options, onChange, disabled }: ChoiceProps<T>) {
  return (
    <label className={FIELD_CLASS}>
      <span className={LABEL_CLASS}>{label}</span>
      <select
        className={`${VALUE_CLASS} cursor-pointer`}
        value={value}
        disabled={disabled}
        onChange={event => {
          const next = options.find(option => option === event.target.value);
          if (next) onChange(next);
        }}
      >
        {options.map(option => (
          <option key={option} value={option} className="bg-[#1a1a1a]">
            {option}
          </option>
        ))}
      </select>
    </label>
  );
}

export function SegmentedControl<T extends string>({ label, value, options, onChange, disabled }: ChoiceProps<T>) {
  return (
    <div className="flex flex-col gap-1 rounded-xl border border-[#595959] p-1.5 pt-[5px]">
      <span className={`${LABEL_CLASS} px-1`}>{label}</span>
      <div role="radiogroup" aria-label={label} className="flex gap-1">
        {options.map(option => (
          <button
            key={option}
            type="button"
            role="radio"
            aria-checked={option === value}
            disabled={disabled}
            onClick={() => onChange(option)}
            className={`h-7 flex-1 rounded-lg text-[11px] font-medium transition-colors disabled:opacity-50 ${
              option === value ? 'bg-white text-black' : 'text-white/60 hover:bg-white/5'
            }`}
          >
            {option}
          </button>
        ))}
      </div>
    </div>
  );
}
