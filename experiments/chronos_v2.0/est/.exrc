set nocompatible
let s:cpo_save=&cpo
set cpo&vim
map! <xHome> <Home>
map! <xEnd> <End>
map! <S-xF4> <S-F4>
map! <S-xF3> <S-F3>
map! <S-xF2> <S-F2>
map! <S-xF1> <S-F1>
map! <xF4> <F4>
map! <xF3> <F3>
map! <xF2> <F2>
map! <xF1> <F1>
map ; $
map ^X :.,$d^M
map <xHome> <Home>
map <xEnd> <End>
map <S-xF4> <S-F4>
map <S-xF3> <S-F3>
map <S-xF2> <S-F2>
map <S-xF1> <S-F1>
map <xF4> <F4>
map <xF3> <F3>
map <xF2> <F2>
map <xF1> <F1>
imap jj l
iabbr teh the
iabbr /// //////////////////////////////////////////////////////////////////////////////
iabbr #e  ***********************************************************************************/
iabbr #b /************************************************************************************
let &cpo=s:cpo_save
unlet s:cpo_save
set autoindent
set background=dark
set backspace=indent,eol,start
set cinoptions=:0,g0
set formatoptions=tcqor
set incsearch
set shiftwidth=4
set showmatch
set softtabstop=4
if &syntax != 'c'
set syntax=c
endif
set textwidth=85
set whichwrap=h,l
