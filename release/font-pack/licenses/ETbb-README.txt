This package, derived from Daniel Benjamin Miller's XETBook, which is in turn derived from ETBook, a font commissioned by Edward Tufte for use in his own books,
provides a version of Bembo in otf and pfb formats plus LaTeX font support files. As ETBook is licensed under the MIT license, so are the font files in this package. See the LICENSE file in the doc folder for details of the MIT license. All other support files are subject to the LaTeX Project Public License. See 
http://www.ctan.org/license/lppl1.3
for the details of that license. Note that Bembo is a name trademarked by Monotype, hence the strange name for this package, drawn from the Berry id for the Bembo font family.

Current version: 1.057 2023-12-10

Changes in version 1.057
1. Modified spacing and kerning of a number of glyphs in order to prevent collisions with parentheses and brackets.
2. Modified the placements of some accents on lowercase letters where the previous positions seemed too close.
3. Added a question.alt glyph with larger left sidebearing, available only using xelatex and lualated though ss09.

Changes in version 1.056
1. Added definitions of \nustyle and \destyle to ETbb.sty.
2. Corrected behavior of option sharpS in legacy LaTeX..
3. Corrected misbehavior of \Qswash and \Qnoswash.
4. Extended small cap coverage in all styles so that all 
T1 encoded characters are available.

Changes in version 1.055
1. Further revision of the kerning tables in regular style, mostly for uppercase and small cap glyphs.
2. Added aeacute.sc glyph.
3. Corrected for some missing c2sc and smscp lookup table entries.
4. Added a fraction.alt glyph to allow a decent fraction
construction in unicode latex. (Regular style only, so far.)

Changes in version 1.054
Version 1.053 seems to have corrupted the kerning tables. 
This version restores the kerning tables to their values in version
1.051.

Changes in version 1.053
Modified a number of settings in the otf files in order that the otf fonts work as expected in Windows 10. (Thanks Espen Solheim.)

Changes in version 1.051
Modified the \textcircled macro so as to scale as expected.

Changes in version 1.05
1. Added swashQ glyph to all faces, along with changes to latex support to enable it. See the documentation.
2. Full support now enabled for all features in LY1 and OT1 as well as T1.

Changes in version 1.04
1. Added glyph numero (U+2116) in all styles.
2. Updated code for \textfrac. See documentation for details.

Changes in version 1.03
Removed all hinting in the font files. This will not affect Mac or PC users, but should be helpful to some Linux users whose font renderers can behave badly at times with Postscript hinting.

Changes in version 1.02
1. Added denominator figures to all styles.
2. Changed the \fraction macro to use denominators and allow finer tuning of spacing between figures and the fractionsolidus.
3. Filled out the glyph slots for symbols in the TS1 encoding so the sub-encoding is close to full.
4. Added newstyle dollar and cent glyphs and added option osdollar to select the oldstyle dollar and cent as the default.

Changes in version 1.01
1. Reverted the changes in 1.001 so that sty file is ETbb.sty and fontspec file is ETbb.fontspec. The prior change was muddled. 
2. Added option osf as a synonym for oldstyle. (Thanks Axel Kielhorn.)
2. Documentation changes to reflect the changes. 
3. Modified some glitches in some BoldItalic figure outlines. (Thanks Daniel Benjamin Miller.)

Changes in version 1.001
Changed the package name from ETbb to etbb, though the font names and the map file name continue to use ETbb.


Please send comments and bug reports or suggestions for improvement to

msharpe at ucsd dot edu