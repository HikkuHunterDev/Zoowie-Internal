export interface Charm {
  Kit: number;
  Pattern: number;
  Highlight: number;
  OffsetX: number;
  OffsetY: number;
  OffsetZ: number;
}

export interface Item {
  Definition: number;
  Slot: number;
  Teams: number;
  Paintable: boolean;
  NameTag: boolean;
  StatTrak: boolean;
  Legacy: boolean;
  StickerSlots: number;
  CharmSlots: number;
  PatchSlots: number;
}

export type ItemKind = 'weapon' | 'knife' | 'gloves' | 'agent' | 'music' | 'medal';

export interface ItemRecord {
  Team: number;
  Slot: number;
  Definition: number;
  Paint: number;
  Seed: number;
  Wear: number;
  Legacy: boolean;
  StatTrak: boolean;
  StatTrakCount: number;
  Name: string;
  Stickers: Sticker[];
  Patches: Patch[];
  Charm?: Charm;
  SouvenirEvent: number;
  Music: number;
  Shown: boolean;
}

export interface Kit {
  Id: number;
  Name: string;
  Type: string;
}

export type KitKind = 'sticker' | 'patch' | 'keychain' | 'music';

export interface LoadoutItem {
  Id: string;
  Record: ItemRecord;
  InSlot: boolean;
}

export interface Paint {
  Id: number;
  Name: string;
  WearMin: number;
  WearMax: number;
}

export interface Patch {
  Slot: number;
  Kit: number;
}

export interface SchemaIds {
  Knife: number;
  Agent: number;
  Gloves: number;
  Music: number;
  Medal: number;
  AnyTeam: number;
  Terrorists: number;
  CounterTerrorists: number;
  Unique: number;
  Strange: number;
  Unusual: number;
  Tournament: number;
}

export interface Sticker {
  Slot: number;
  Kit: number;
  Wear: number;
  Scale: number;
  Rotation: number;
  OffsetX: number;
  OffsetY: number;
  Schema: number;
}

export interface StickerPlacement {
  Schema: number;
  OffsetX: number;
  OffsetY: number;
  Rotation: number;
}

export interface OpenChangerMethods {
  changer: {
    items(kind: ItemKind): Item[];
    paints(definition: number): Paint[];
    kits(kind: KitKind): Kit[];
    souvenirEvents(): number[];
    ids(): SchemaIds;
    loadout(): LoadoutItem[];
    equip(record: ItemRecord): string;
    unequip(record: ItemRecord): void;
    showMedal(definition: number): string;
    removeMedal(definition: number): void;
    preview(record: ItemRecord): string;
    endPreview(): void;
    draftId(): string;
    favorites(): string[];
    favorite(key: string, on: boolean): void;
    stickerPlacement(): StickerPlacement | undefined;
  };
  dev: {
    log(text: string): void;
  };
  events: {
    subscribe(callback: (feature: string, event: string) => void): number | undefined;
    unsubscribe(id: number): void;
  };
}
