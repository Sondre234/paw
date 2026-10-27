-- tekketsu (鉄血) for Neovim: iron and blood. Bone text on cracked black iron, crimson for the
-- keywords, blood red for the cursor and whatever has focus, brass, the violet gunmetal of
-- mechanical arms and the green of an elf's eyes for the rest. The editor paints no background
-- of its own, so kitty's translucency (and paw's blur behind it) shows through the text.
-- tekketsu's palette is its own; the scheme follows the structure of paw's mochi scheme and is
-- paw's, GPL-3.0-or-later.
--
-- Plugin-free. Copy this file to ~/.config/nvim/colors/ (or put the editors/nvim directory on
-- 'runtimepath') and run `:colorscheme tekketsu`. True colour with 'termguicolors' (Neovim's
-- default in kitty); without it, the ANSI indices match the tekketsu kitty palette. In a
-- terminal without translucency the page is the terminal's own background: set it to
-- tekketsu's base, #100e0e.
--
-- The large backgrounds it does paint are these, and tekketsu's kitty theme lists every one of
-- them in transparent_background_colors, so they stay glass too instead of turning into slabs:
--   #0B0A0A  mantle: floats, menus, the status lines and the tab line
--   #1E1A1A  the cursor line and the colour column, a shade above surface0
--   #231F1F  surface1: closed folds, the menu's scrollbar track and snippet tabstops
--   #2F2929  surface2: the selected menu item, the current tab, matching brackets, other uses
--            of the word under the cursor and the current quickfix line
--   #3C1714  the selection, a dried-blood wash under text that keeps its colours
--   #2A1312  Helix's other selections (tekketsu.toml); Neovim has only the one
-- Everything else it paints is a small solid mark: the cursor, the current search hit, TODO
-- tags, the menu's scrollbar thumb, the shadow under floats that ask for one, and the washes of
-- colour behind search hits, diffs and diagnostics.
--
-- tekketsu's style: keywords in crimson, functions in bone and bold (their definitions in
-- parchment), types in brass, strings in steel, numbers and constants in rose, builtins in
-- ichor, parameters in elf green, variables in subtext, operators and punctuation in overlay,
-- comments in overlay italic; the cursor and the focus in blood, links and info in frost.

-- Each colour is { truecolor, ANSI index }. An index of nil leaves the terminal's own colour.
local p = {
  -- The grounds, cracked black iron to bone, a warm near-neutral black
  crust     = { "#060505", 0 },
  mantle    = { "#0B0A0A", 0 },
  base      = { "#100E0E", 0 },
  surface0  = { "#191616", 0 },
  surface1  = { "#231F1F", 0 },
  surface2  = { "#2F2929", 8 },
  overlay0  = { "#463D3D", 8 },
  overlay1  = { "#6B605E", 8 },
  overlay2  = { "#8E827D", 7 },
  subtext0  = { "#AB9F90", 7 },
  subtext1  = { "#CBBFA8", 7 },
  text      = { "#E4DBC4", 15 },

  -- The accents
  blood     = { "#C42B23", 1 },   -- the accent: the cursor, focus, the current search hit
  gore      = { "#6E1712", 1 },   -- deep red, a rare mark
  crimson   = { "#E5483D", 1 },   -- the readable red: keywords, errors and deletions
  ichor     = { "#FF6A5C", 9 },   -- the brightest red: builtins, macros and directives
  parchment = { "#E6DEB1", 11 },  -- function definitions, bold text
  brass     = { "#CFA772", 3 },   -- types and warnings
  elf       = { "#A7B98C", 2 },   -- parameters, additions, regular expressions
  steel     = { "#A196AD", 5 },   -- strings
  frost     = { "#9CB2C9", 4 },   -- links, info, modules
  bruise    = { "#8C6F8F", 5 },   -- labels, sparingly

  -- Blends of them: numbers, the cursor line, the selection, and washes of colour behind search
  -- hits, diffs and diagnostics (each the accent over base). The washes behind search hits and
  -- diffs are stronger than an opaque page would need, so they still read where light wallpaper
  -- brightens the glass around them.
  rose      = { "#F49786", 9 },   -- numbers and constants: ichor paled with bone, blood on bone
  ash       = { "#584E4D", 8 },   -- inlay hints: between overlay0 and overlay1, under comments
  line      = { "#1E1A1A" },      -- the cursor line: a shade above surface0, so it reads on glass
  sel       = { "#3C1714", 0 },   -- the selection: a dried-blood wash, as in kitty
  search    = { "#581A16", 1 },   -- search hits: blood at 40% over base
  search_fg = { "#E4DBC4", 15 },  -- text on search hits
  add_bg    = { "#34372C" },      -- elf 24%
  del_bg    = { "#431C19" },      -- crimson 24%
  change_bg = { "#2B231C" },      -- brass 14%
  text_bg   = { "#493C2C" },      -- changed text in a changed line: brass 30%
  crimson_bg = { "#251413" },     -- crimson 10%, behind virtual text
  brass_bg  = { "#231D18" },
  frost_bg  = { "#1E1E21" },
  hint_bg   = { "#1D1A19" },
  elf_bg    = { "#1F1F1B" },

  shadow_blend = 60, -- how much a float's shadow lets through, 0-100
}

local ansi = {
  "#231F1F", "#E5483D", "#A7B98C", "#CFA772", "#9CB2C9", "#A196AD", "#8FADA5", "#CBBFA8",
  "#6B605E", "#FF6A5C", "#BCCDA2", "#E6DEB1", "#B6C8DA", "#BCB1C7", "#AAC6BE", "#E4DBC4",
}

vim.cmd("highlight clear")
if vim.fn.exists("syntax_on") == 1 then vim.cmd("syntax reset") end
vim.o.background = "dark"
vim.g.colors_name = "tekketsu"

-- hl(group, spec): fg/bg/sp take palette colours and fill in both the truecolor and the ANSI
-- value; other keys (bold, link, cterm, ctermfg...) pass through to nvim_set_hl unchanged.
local function hl(group, spec)
  local o = {}
  for k, v in pairs(spec) do
    if k == "fg" or k == "bg" or k == "sp" then
      o[k] = v[1]
      if k ~= "sp" and v[2] and o["cterm" .. k] == nil then o["cterm" .. k] = v[2] end
    else
      o[k] = v
    end
  end
  vim.api.nvim_set_hl(0, group, o)
end

local groups = {
  -- Editor: no background, so the glass shows ------------------------------------------------
  Normal           = { fg = p.text },
  NormalNC         = { fg = p.text },
  NormalFloat      = { fg = p.text, bg = p.mantle },
  FloatBorder      = { fg = p.overlay0, bg = p.mantle },
  FloatTitle       = { fg = p.crimson, bg = p.mantle, bold = true },
  FloatFooter      = { fg = p.overlay1, bg = p.mantle },
  FloatShadow      = { bg = p.crust, blend = p.shadow_blend },
  FloatShadowThrough = { bg = p.crust, blend = 100 },
  Cursor           = { fg = p.text, bg = p.blood },
  lCursor          = { link = "Cursor" },
  CursorIM         = { link = "Cursor" },
  TermCursor       = { fg = p.text, bg = p.blood },
  CursorLine       = { bg = p.line },
  CursorColumn     = { bg = p.line },
  ColorColumn      = { bg = p.line },
  LineNr           = { fg = p.overlay0 },
  LineNrAbove      = { link = "LineNr" },
  LineNrBelow      = { link = "LineNr" },
  CursorLineNr     = { fg = p.crimson, bold = true },
  CursorLineFold   = { fg = p.crimson },
  CursorLineSign   = {},
  SignColumn       = { fg = p.surface2 },
  FoldColumn       = { fg = p.overlay0 },
  Folded           = { fg = p.subtext0, bg = p.surface1, italic = true },
  WinSeparator     = { fg = p.surface2 },
  VertSplit        = { link = "WinSeparator" },
  StatusLine       = { fg = p.text, bg = p.mantle },
  StatusLineNC     = { fg = p.overlay1, bg = p.mantle },
  StatusLineTerm   = { link = "StatusLine" },
  StatusLineTermNC = { link = "StatusLineNC" },
  WinBar           = { fg = p.text, bold = true },
  WinBarNC         = { fg = p.overlay1 },
  TabLine          = { fg = p.overlay1, bg = p.mantle },
  TabLineSel       = { fg = p.crimson, bg = p.surface2, bold = true },
  TabLineFill      = {},
  Pmenu            = { fg = p.subtext0, bg = p.mantle },
  PmenuSel         = { fg = p.text, bg = p.surface2, bold = true },
  PmenuKind        = { fg = p.frost, bg = p.mantle },
  PmenuKindSel     = { fg = p.frost, bg = p.surface2, bold = true },
  PmenuExtra       = { fg = p.overlay1, bg = p.mantle },
  PmenuExtraSel    = { fg = p.overlay2, bg = p.surface2 },
  PmenuMatch       = { fg = p.crimson, bg = p.mantle, bold = true },
  PmenuMatchSel    = { fg = p.crimson, bg = p.surface2, bold = true },
  PmenuSbar        = { bg = p.surface1 },
  PmenuThumb       = { bg = p.overlay0 },
  ComplMatchIns    = { fg = p.overlay1 },
  WildMenu         = { link = "PmenuSel" },
  Visual           = { bg = p.sel, bold = true, cterm = { reverse = true } },
  VisualNOS        = { link = "Visual" },
  VisualNC         = { link = "Visual" },
  Search           = { fg = p.search_fg, bg = p.search },
  CurSearch        = { fg = p.text, bg = p.blood, bold = true },
  IncSearch        = { link = "CurSearch" },
  Substitute       = { fg = p.crust, bg = p.crimson },
  MatchParen       = { fg = p.rose, bg = p.surface2, bold = true, cterm = { bold = true, underline = true } },
  QuickFixLine     = { bg = p.surface2, bold = true },
  NonText          = { fg = p.overlay0 },
  EndOfBuffer      = { fg = p.surface1 },
  Whitespace       = { fg = p.surface2 },
  SpecialKey       = { fg = p.overlay0 },
  Conceal          = { fg = p.overlay1 },
  Directory        = { fg = p.frost },
  Title            = { fg = p.crimson, bold = true },
  Question         = { fg = p.frost },
  MoreMsg          = { fg = p.frost },
  ModeMsg          = { fg = p.text, bold = true },
  MsgArea          = {},
  MsgSeparator     = { fg = p.surface2 },
  OkMsg            = { fg = p.elf },
  WarningMsg       = { fg = p.brass },
  ErrorMsg         = { fg = p.crimson, bold = true, italic = true },
  SpellBad         = { undercurl = true, sp = p.crimson },
  SpellCap         = { undercurl = true, sp = p.brass },
  SpellLocal       = { undercurl = true, sp = p.frost },
  SpellRare        = { undercurl = true, sp = p.bruise },
  DiffAdd          = { bg = p.add_bg, ctermfg = p.elf[2] },
  DiffChange       = { bg = p.change_bg, ctermfg = p.brass[2] },
  DiffDelete       = { fg = p.crimson, bg = p.del_bg },
  DiffText         = { bg = p.text_bg, ctermfg = p.brass[2] },
  Added            = { fg = p.elf },
  Changed          = { fg = p.brass },
  Removed          = { fg = p.crimson },
  SnippetTabstop   = { bg = p.surface1 },
  RedrawDebugNormal = { reverse = true },

  -- Diagnostics -------------------------------------------------------------------------------
  DiagnosticError            = { fg = p.crimson },
  DiagnosticWarn             = { fg = p.brass },
  DiagnosticInfo             = { fg = p.frost },
  DiagnosticHint             = { fg = p.overlay2 },
  DiagnosticOk               = { fg = p.elf },
  DiagnosticVirtualTextError = { fg = p.crimson, bg = p.crimson_bg, italic = true },
  DiagnosticVirtualTextWarn  = { fg = p.brass, bg = p.brass_bg, italic = true },
  DiagnosticVirtualTextInfo  = { fg = p.frost, bg = p.frost_bg, italic = true },
  DiagnosticVirtualTextHint  = { fg = p.overlay2, bg = p.hint_bg, italic = true },
  DiagnosticVirtualTextOk    = { fg = p.elf, bg = p.elf_bg, italic = true },
  DiagnosticVirtualLinesError = { fg = p.crimson },
  DiagnosticVirtualLinesWarn = { fg = p.brass },
  DiagnosticVirtualLinesInfo = { fg = p.frost },
  DiagnosticVirtualLinesHint = { fg = p.overlay2 },
  DiagnosticVirtualLinesOk   = { fg = p.elf },
  DiagnosticUnderlineError   = { undercurl = true, sp = p.crimson },
  DiagnosticUnderlineWarn    = { undercurl = true, sp = p.brass },
  DiagnosticUnderlineInfo    = { undercurl = true, sp = p.frost },
  DiagnosticUnderlineHint    = { undercurl = true, sp = p.overlay2 },
  DiagnosticUnderlineOk      = { undercurl = true, sp = p.elf },
  DiagnosticSignError        = { fg = p.crimson },
  DiagnosticSignWarn         = { fg = p.brass },
  DiagnosticSignInfo         = { fg = p.frost },
  DiagnosticSignHint         = { fg = p.overlay2 },
  DiagnosticSignOk           = { fg = p.elf },
  DiagnosticFloatingError    = { fg = p.crimson },
  DiagnosticFloatingWarn     = { fg = p.brass },
  DiagnosticFloatingInfo     = { fg = p.frost },
  DiagnosticFloatingHint     = { fg = p.overlay2 },
  DiagnosticFloatingOk       = { fg = p.elf },
  DiagnosticUnnecessary      = { fg = p.overlay1 },
  DiagnosticDeprecated       = { strikethrough = true, sp = p.overlay1 },

  -- LSP ---------------------------------------------------------------------------------------
  LspReferenceText            = { bg = p.surface2 },
  LspReferenceRead            = { bg = p.surface2 },
  LspReferenceWrite           = { bg = p.surface2, underline = true, sp = p.blood },
  LspReferenceTarget          = { link = "LspReferenceText" },
  LspInlayHint                = { fg = p.ash, italic = true },
  LspCodeLens                 = { fg = p.ash, italic = true },
  LspCodeLensSeparator        = { fg = p.surface2 },
  LspSignatureActiveParameter = { fg = p.rose, bold = true, underline = true },

  -- Syntax (legacy groups, still used by every non-Tree-sitter filetype) ------------------------
  Comment        = { fg = p.overlay1, italic = true },
  Constant       = { fg = p.rose },
  String         = { fg = p.steel },
  Character      = { fg = p.steel },
  Number         = { fg = p.rose },
  Boolean        = { fg = p.rose },
  Float          = { fg = p.rose },
  Identifier     = { fg = p.subtext1 },
  Function       = { fg = p.text, bold = true },
  Statement      = { fg = p.crimson },
  Conditional    = { fg = p.crimson },
  Repeat         = { fg = p.crimson },
  Label          = { fg = p.bruise },
  Operator       = { fg = p.overlay2 },
  Keyword        = { fg = p.crimson },
  Exception      = { fg = p.crimson },
  PreProc        = { fg = p.ichor },
  Include        = { fg = p.crimson },
  Define         = { fg = p.ichor },
  Macro          = { fg = p.ichor },
  PreCondit      = { fg = p.ichor },
  Type           = { fg = p.brass },
  StorageClass   = { fg = p.crimson },
  Structure      = { fg = p.crimson },
  Typedef        = { fg = p.crimson },
  Special        = { fg = p.rose },
  SpecialChar    = { fg = p.rose },
  Tag            = { fg = p.crimson },
  Delimiter      = { fg = p.overlay2 },
  SpecialComment = { fg = p.overlay2, bold = true, italic = true },
  Debug          = { fg = p.ichor },
  Underlined     = { fg = p.frost, underline = true },
  Ignore         = { fg = p.overlay0 },
  Error          = { fg = p.crimson, bold = true },
  Todo           = { fg = p.text, bg = p.blood, bold = true },

  -- Tree-sitter -------------------------------------------------------------------------------
  ["@variable"]                   = { fg = p.subtext1 },
  ["@variable.builtin"]           = { fg = p.ichor },
  ["@variable.parameter"]         = { fg = p.elf },
  ["@variable.parameter.builtin"] = { fg = p.elf, italic = true },
  ["@variable.member"]            = { fg = p.text },
  ["@property"]                   = { fg = p.text },

  ["@constant"]         = { fg = p.rose },
  ["@constant.builtin"] = { fg = p.rose },
  ["@constant.macro"]   = { fg = p.rose },

  ["@module"]         = { fg = p.frost, italic = true },
  ["@module.builtin"] = { fg = p.frost, italic = true },
  ["@label"]          = { fg = p.bruise },

  ["@string"]               = { fg = p.steel },
  ["@string.documentation"] = { fg = p.steel, italic = true },
  ["@string.regexp"]        = { fg = p.elf },
  ["@string.escape"]        = { fg = p.rose },
  ["@string.special"]       = { fg = p.rose },
  ["@string.special.symbol"] = { fg = p.rose },
  ["@string.special.path"]  = { fg = p.steel, underline = true },
  ["@string.special.url"]   = { fg = p.frost, underline = true },
  ["@character"]            = { fg = p.steel },
  ["@character.special"]    = { fg = p.rose },

  ["@boolean"]      = { fg = p.rose },
  ["@number"]       = { fg = p.rose },
  ["@number.float"] = { fg = p.rose },

  ["@type"]            = { fg = p.brass },
  ["@type.builtin"]    = { fg = p.brass, italic = true },
  ["@type.definition"] = { fg = p.brass },

  ["@attribute"]         = { fg = p.brass },
  ["@attribute.builtin"] = { fg = p.brass },

  -- A function's name is parchment where it is defined, and stays bone where it is called; both
  -- bold.
  ["@function"]             = { fg = p.parchment, bold = true },
  ["@function.builtin"]     = { fg = p.ichor, bold = true },
  ["@function.call"]        = { fg = p.text, bold = true },
  ["@function.macro"]       = { fg = p.ichor },
  ["@function.method"]      = { fg = p.parchment, bold = true },
  ["@function.method.call"] = { fg = p.text, bold = true },
  ["@constructor"]          = { fg = p.brass },
  ["@constructor.lua"]      = { link = "@punctuation.bracket" }, -- a table's braces
  ["@operator"]             = { fg = p.overlay2 },

  ["@keyword"]                     = { fg = p.crimson },
  ["@keyword.coroutine"]           = { fg = p.crimson },
  ["@keyword.function"]            = { fg = p.crimson },
  ["@keyword.operator"]            = { fg = p.crimson },
  ["@keyword.import"]              = { fg = p.crimson },
  ["@keyword.type"]                = { fg = p.crimson },
  ["@keyword.modifier"]            = { fg = p.crimson },
  ["@keyword.repeat"]              = { fg = p.crimson },
  ["@keyword.return"]              = { fg = p.crimson },
  ["@keyword.debug"]               = { fg = p.ichor },
  ["@keyword.exception"]           = { fg = p.crimson },
  ["@keyword.conditional"]         = { fg = p.crimson },
  ["@keyword.conditional.ternary"] = { fg = p.crimson },
  ["@keyword.directive"]           = { fg = p.ichor },
  ["@keyword.directive.define"]    = { fg = p.ichor },

  ["@punctuation.delimiter"] = { fg = p.overlay2 },
  ["@punctuation.bracket"]   = { fg = p.overlay2 },
  ["@punctuation.special"]   = { fg = p.rose },

  ["@comment"]               = { link = "Comment" },
  ["@comment.documentation"] = { link = "Comment" },
  ["@comment.error"]         = { fg = p.crust, bg = p.crimson, bold = true },
  ["@comment.warning"]       = { fg = p.crust, bg = p.brass, bold = true },
  ["@comment.todo"]          = { fg = p.text, bg = p.blood, bold = true },
  ["@comment.note"]          = { fg = p.crust, bg = p.frost, bold = true },

  ["@markup.strong"]           = { fg = p.parchment, bold = true },
  ["@markup.italic"]           = { italic = true },
  ["@markup.strikethrough"]    = { fg = p.overlay2, strikethrough = true },
  ["@markup.underline"]        = { underline = true },
  ["@markup.heading"]          = { fg = p.crimson, bold = true },
  ["@markup.heading.1"]        = { fg = p.crimson, bold = true },
  ["@markup.heading.2"]        = { fg = p.rose, bold = true },
  ["@markup.heading.3"]        = { fg = p.brass, bold = true },
  ["@markup.heading.4"]        = { fg = p.elf, bold = true },
  ["@markup.heading.5"]        = { fg = p.steel, bold = true },
  ["@markup.heading.6"]        = { fg = p.frost, bold = true },
  ["@markup.quote"]            = { fg = p.subtext0, italic = true },
  ["@markup.math"]             = { fg = p.brass },
  ["@markup.link"]             = { fg = p.frost },
  ["@markup.link.label"]       = { fg = p.frost },
  ["@markup.link.url"]         = { fg = p.frost, italic = true, underline = true },
  ["@markup.raw"]              = { fg = p.steel },
  ["@markup.raw.block"]        = { fg = p.subtext1 },
  ["@markup.list"]             = { fg = p.crimson },
  ["@markup.list.checked"]     = { fg = p.elf },
  ["@markup.list.unchecked"]   = { fg = p.overlay1 },
  ["@markup.environment"]      = { fg = p.crimson },

  ["@diff.plus"]  = { link = "Added" },
  ["@diff.minus"] = { link = "Removed" },
  ["@diff.delta"] = { link = "Changed" },

  ["@tag"]           = { fg = p.crimson },
  ["@tag.builtin"]   = { fg = p.crimson },
  ["@tag.attribute"] = { fg = p.brass },
  ["@tag.delimiter"] = { fg = p.overlay2 },

  -- LSP semantic tokens: follow the Tree-sitter captures, so a language server only refines.
  -- @lsp.type.variable stays empty so Tree-sitter's builtins and parameters keep their colour.
  -- A server marks every use of a function the same way, so its functions and methods follow
  -- the calls, and only the ones it marks as a declaration or definition glow like @function.
  ["@lsp.type.class"]         = { link = "@type" },
  ["@lsp.type.comment"]       = { link = "@comment" },
  ["@lsp.type.decorator"]     = { link = "@attribute" },
  ["@lsp.type.enum"]          = { link = "@type" },
  ["@lsp.type.enumMember"]    = { link = "@constant" },
  ["@lsp.type.event"]         = { link = "@type" },
  ["@lsp.type.function"]      = { link = "@function.call" },
  ["@lsp.type.interface"]     = { link = "@type" },
  ["@lsp.type.keyword"]       = { link = "@keyword" },
  ["@lsp.type.macro"]         = { link = "@constant.macro" },
  ["@lsp.type.method"]        = { link = "@function.method.call" },
  ["@lsp.type.modifier"]      = { link = "@keyword.modifier" },
  ["@lsp.type.namespace"]     = { link = "@module" },
  ["@lsp.type.number"]        = { link = "@number" },
  ["@lsp.type.operator"]      = { link = "@operator" },
  ["@lsp.type.parameter"]     = { link = "@variable.parameter" },
  ["@lsp.type.property"]      = { link = "@property" },
  ["@lsp.type.regexp"]        = { link = "@string.regexp" },
  ["@lsp.type.string"]        = { link = "@string" },
  ["@lsp.type.struct"]        = { link = "@type" },
  ["@lsp.type.type"]          = { link = "@type" },
  ["@lsp.type.typeParameter"] = { link = "@type.definition" },
  ["@lsp.type.variable"]      = {},
  ["@lsp.mod.deprecated"]     = { strikethrough = true },
  ["@lsp.typemod.function.declaration"]    = { link = "@function" },
  ["@lsp.typemod.function.definition"]     = { link = "@function" },
  ["@lsp.typemod.method.declaration"]      = { link = "@function.method" },
  ["@lsp.typemod.method.definition"]       = { link = "@function.method" },
  ["@lsp.typemod.function.defaultLibrary"] = { link = "@function.builtin" },
  ["@lsp.typemod.method.defaultLibrary"]   = { link = "@function.builtin" },
  ["@lsp.typemod.variable.defaultLibrary"] = { link = "@variable.builtin" },
}

for group, spec in pairs(groups) do hl(group, spec) end

for i, c in ipairs(ansi) do vim.g["terminal_color_" .. (i - 1)] = c end
