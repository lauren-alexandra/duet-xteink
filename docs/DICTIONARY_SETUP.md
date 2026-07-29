# Dictionary setup

Duet reads offline dictionaries in uncompressed StarDict format. The easiest option for English lookup is the ready-to-copy WordNet 3.0 pack attached to the current Duet release.

## Install the included WordNet pack

1. Download `Duet-WordNet-3.0-StarDict.zip` from the current [Duet release](https://github.com/lauren-alexandra/duet-xteink/releases).
2. Back up the SD card before changing it.
3. Extract the ZIP on the computer. It contains a top-level `dictionaries` folder.
4. Copy that `dictionaries` folder to the root of the SD card. Merge it with an existing folder if macOS asks.
5. Confirm the card now contains this exact structure:

```text
SD Card Root/
└── dictionaries/
    └── English/
        ├── princeton-wordnet-3.0.ifo
        ├── princeton-wordnet-3.0.idx
        ├── princeton-wordnet-3.0.dict
        ├── princeton-wordnet-3.0.syn
        └── WORDNET-LICENSE.txt
```

6. Eject the SD card cleanly, put it in the reader, and start Duet.
7. Open **Apps > Dictionary**. Select **WordNet 3.0**, then let **Preparing dictionary** finish. The first preparation builds a small adjacent `.cpridx` cache and can take a little time.
8. When Duet says **Dictionary ready**, open a book and press **Confirm** to open the reader overlay.
9. Choose **Dictionary**. Use the page buttons to move between text rows, **Left/Right** to move between words, and **Confirm** to look up the selected word.

The full reader menu also has **Lookup history**, which keeps the words looked up in that book.

## Install another StarDict dictionary

Duet scans one folder level below `/dictionaries`. Each dictionary needs its own folder and at least three files with the exact same basename:

```text
SD Card Root/
└── dictionaries/
    └── My Dictionary/
        ├── MyDictionary.ifo
        ├── MyDictionary.idx
        ├── MyDictionary.dict
        └── MyDictionary.syn       # optional
```

Use a dictionary that you are licensed to download and copy. Duet supports an uncompressed `.dict` file. A package containing only `.dict.dz` must be extracted on the computer first:

```bash
gzip -dc "MyDictionary.dict.dz" > "MyDictionary.dict"
```

Keep the `.ifo`, `.idx`, and resulting `.dict` together with identical spelling and capitalization before copying the folder to the card.

## Troubleshooting

### No dictionaries found

Check that the `.ifo` file is exactly one folder below `/dictionaries`. Duet scans `/dictionaries/English/Example.ifo`; it will not find `/dictionaries/English/Another Folder/Example.ifo`.

### Dictionary files missing

The `.ifo`, `.idx`, and `.dict` files must all exist and share the same basename. `Example.ifo` will not pair with `example.idx` on a case-sensitive card.

### Compressed dictionaries are not supported

The package still has `.dict.dz` instead of an uncompressed `.dict`. Extract it on the computer with the command above, then copy the uncompressed file to the dictionary folder.

### Preparation fails or never finishes

Eject the card, remount it on the computer, and confirm the three required files have nonzero sizes and the card has free space. A `.cpridx` file is only Duet's derived index cache; after backing up the card, deleting the matching `.cpridx` while the reader is off forces Duet to rebuild it on the next preparation.

### Definitions show question marks

That usually means the dictionary uses an unsupported character encoding, unusual markup, or glyphs absent from the selected UI font. Try a UTF-8 StarDict package and a font with the needed language coverage. The included WordNet 3.0 pack uses plain English text and is the best first test.

### Lookup finds no exact match

Use the suggestion list when Duet offers one. Inflected words, punctuation, capitalization, and hyphenation can prevent an exact headword match even when a related entry exists.

## WordNet source and license

The optional release asset is a StarDict conversion of WordNet 3.0. The archive keeps the original WordNet license beside the dictionary files. WordNet permits copying, modification, and redistribution when its copyright notice, license terms, and disclaimer remain with every copy. See [Princeton's WordNet license](https://wordnet.princeton.edu/license-and-commercial-use) and the license included in the download.
