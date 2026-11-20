// Zmiana trybu przez Wi-Fi: strona http://zegar-matrix.local (albo IP zegara).
// Dla automatyzacji: GET /mode?m=clock|random|gif|claude|lava|life|music|image|pc|rates|
//                    analog|maze|show
//                    GET /gif?i=random|0|1|...  (wybor GIF-a, wlacza tryb GIF)
//                    GET /ai?s=work|wait|done|idle (stan Claude; work/wait wlacza panel Claude)
//                    GET /ai?auto=0|1  (automatyczne przelaczanie na panel Claude, ze strony)
//                    GET /claude?h=41.5&hr=<epoch>&w=12&wr=<epoch>&c=23&usd=1.23&m=Opus%205.5
//                        (panel Claude; wysyla skrypt statusline, kazdy parametr opcjonalny:
//                         h/w = zuzyte % limitu 5 h / 7 dni, hr/wr = reset (Unix s),
//                         c = zuzyty % kontekstu, usd = koszt sesji, m = model,
//                         ct = tokeny kontekstu (gdy % nieznany), s = jak w /ai)
//                    GET /msg?t=TEKST&s=sekundy&c=odcien  (wiadomosc, message_overlay.cpp)
//                    GET /info  (diagnostyka: czas od startu, powod restartu, Wi-Fi, pamiec)
//                    GET /brightness?v=0..8  (jasnosc, 0 = ekran wylaczony; brightness.cpp)
//                    GET /night?set=1&s=23&e=6&dim=1&nc=0..3  (tryb nocny), /night?wake=1 (wlacz do rana)
//                    GET /manga?n=7  (nieprzeczytane rozdzialy MangaDex, pomaranczowa liczba)
//                    GET /mail?n=3   (nieprzeczytane maile Gmail, czerwona liczba)
//                    GET /live?n=2   (kanaly YouTube na zywo, zielona liczba)
//                    POST /counters url=<adres /exec Apps Script> (liczniki bez komputera,
//                        web_counters.cpp; pusty url = wylacz), GET /counters = stan
//                    GET /np?a=..&t=..&p=ms&d=ms&s=1|0, /np?off=1, POST /cover
//                        (ekran "Teraz grane", mode_music.cpp; wysyla muzyka_zegar.ps1)
//                    GET /pc?c=&g=&r=&ru=&rt=&v=&vt=  (ekran "Komputer", mode_pc.cpp;
//                        wysyla muzyka_zegar.ps1 co 2 s), GET /rates = stan kursow
//                    GET /image?i=N (wybor obrazka), /image?del=N, POST /image (nowy,
//                        RGB 64x64 jako hex - robi to strona; mode_image.cpp)
#include "zegar.h"
#include <WiFi.h>
#include <WebServer.h>
#include <FFat.h>
#include <ESPmDNS.h>

static WebServer _webServer(80);
static bool _webStarted{false};

struct WebMode {
  const char* id;
  const char* label;
  AnimationMode mode;
};

static const WebMode kWebModes[] {
  {"clock", "Zegar", AnimationMode::Clock},
  {"random", "Losowe", AnimationMode::Random},
  {"gif", "GIF", AnimationMode::Gif},
  {"claude", "Claude", AnimationMode::Claude},
  {"lava", "Lava", AnimationMode::Lava},
  {"life", "Gra w życie", AnimationMode::Life},
  {"music", "Teraz grane", AnimationMode::Music},
  {"image", "Obrazek", AnimationMode::Image},
  {"pc", "Komputer", AnimationMode::Pc},
  {"rates", "Kursy", AnimationMode::Rates},
  {"analog", "Zegar analogowy", AnimationMode::Analog},
  {"maze", "Labirynt", AnimationMode::Maze},
  {"show", "🔁 Automat", AnimationMode::Show},
};

// Nazwy z plikow (GIF-y) na stronie - bez znakow HTML.
static String htmlEscape(const char* text) {
  String out;
  for (const char* c = text; *c; c++) {
    switch (*c) {
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '&': out += F("&amp;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += *c;
    }
  }
  return out;
}

// Lista wyboru jednej pozycji Automatu: tryby, kazdy GIF i kazdy obrazek osobno.
// Wartosc "tryb.wariant"; pusta = brak pozycji (dodawanie / usuwanie).
static void appendShowOption(String& page, uint8_t mode, int8_t option, const String& label,
  bool selected) {
  page += F("<option value='");
  page += String(mode);
  page += '.';
  page += String(option);
  page += '\'';
  if (selected) {
    page += F(" selected");
  }
  page += '>';
  page += label;
  page += F("</option>");
}

static void appendShowSelect(String& page, bool used, uint8_t mode, int8_t option) {
  page += F("<select name=e><option value=''>");
  page += used ? F("— usuń —") : F("— dodaj —");
  page += F("</option>");
  for (const WebMode& web : kWebModes) {
    uint8_t value = static_cast<uint8_t>(web.mode);
    if (web.mode == AnimationMode::Show) {
      continue;
    }
    if (web.mode == AnimationMode::Gif) {
      appendShowOption(page, value, -1, F("GIF: losowo"), used && mode == value && option == -1);
      for (uint8_t clip = 0; clip < getGifCount(); clip++) {
        appendShowOption(page, value, clip, String("GIF: ") + htmlEscape(getGifName(clip)),
          used && mode == value && option == clip);
      }
    } else if (web.mode == AnimationMode::Image) {
      for (uint8_t image = 0; image < getImageCount(); image++) {
        appendShowOption(page, value, image, String("Obrazek ") + (image + 1),
          used && mode == value && option == image);
      }
    } else {
      appendShowOption(page, value, 0, web.label, used && mode == value);
    }
  }
  page += F("</select>");
}

static void appendShowSection(String& page) {
  page += F("<h2 id=automat>Automat</h2><p class=info>");
  page += isShowRunning() ? F("Włączony - zegar sam zmienia ekrany po kolei.")
    : F("Zegar sam zmienia ekrany po kolei. Pomija „Teraz grane”, gdy nic nie gra,"
      " i „Komputer”, gdy jest wyłączony.");
  page += F("</p><form class=show action=/show>");
  uint8_t count = getShowCount();
  for (uint8_t index = 0; index <= count && index < kShowMaxEntries; index++) {
    uint8_t mode = 0;
    int8_t option = 0;
    uint16_t seconds = 60;
    bool used = getShowEntry(index, &mode, &option, &seconds);
    page += F("<div class=srow>");
    appendShowSelect(page, used, mode, option);
    page += F("<input type=number name=t min=5 max=3600 step=5 value=");
    page += String(seconds);
    page += F("> s</div>");
  }
  page += F("<div class=srow>Pasek na dole: <select name=bar>");
  static const char* const kBarNames[] {"wyłączony", "stały, nowe migają",
    "tylko przy nowości (30 s)"};
  for (uint8_t bar = 0; bar < 3; bar++) {
    page += F("<option value=");
    page += String(bar);
    if (bar == getNotifyBarMode()) {
      page += F(" selected");
    }
    page += '>';
    page += kBarNames[bar];
    page += F("</option>");
  }
  page += F("</select></div><div class=srow>Obraz pod paskiem: <select name=barfit>"
    "<option value=0");
  if (!getNotifyBarFit()) {
    page += F(" selected");
  }
  page += F(">pasek nakłada się</option><option value=1");
  if (getNotifyBarFit()) {
    page += F(" selected");
  }
  page += F(">obraz zmniejszony</option></select></div>"
    "<p class=info>Pasek: godzina i liczniki na GIF-ach, obrazkach,"
    " Lavie, Grze w życie, Labiryncie i Losowych. Przy zmniejszeniu obraz"
    " zajmuje górne 57 wierszy (GIF-y i obrazki 57x57 na środku).</p>"
    "<div class=srow><button>Zapisz</button>"
    "<button name=run value=1>Zapisz i włącz</button></div></form>");
}

// Grupy trybow na zakladce "Ekran" (numery z AnimationMode).
struct WebModeGroup {
  const char* title;
  uint8_t count;
  AnimationMode modes[6];
};
static const WebModeGroup kWebModeGroups[] {
  {"Zegar i informacje", 6, {AnimationMode::Clock, AnimationMode::Analog, AnimationMode::Claude,
    AnimationMode::Music, AnimationMode::Pc, AnimationMode::Rates}},
  {"Animacje", 4, {AnimationMode::Random, AnimationMode::Lava, AnimationMode::Life,
    AnimationMode::Maze}},
  {"Obrazy", 2, {AnimationMode::Gif, AnimationMode::Image}},
};

static const char* getWebModeLabel(uint8_t modeValue) {
  for (const WebMode& mode : kWebModes) {
    if (static_cast<uint8_t>(mode.mode) == modeValue) {
      return mode.label;
    }
  }
  return "?";
}

static void appendModeButton(String& page, uint8_t modeValue) {
  for (const WebMode& mode : kWebModes) {
    if (static_cast<uint8_t>(mode.mode) != modeValue) {
      continue;
    }
    page += F("<a href='/mode?m=");
    page += mode.id;
    page += '\'';
    if (isShowRunning() ? mode.mode == AnimationMode::Show : mode.mode == _animationMode) {
      page += F(" class=on");
    }
    page += '>';
    page += mode.label;
    page += F("</a>");
  }
}

// Strona: zakladki Ekran / Automat / Obrazy / Noc / Ustawienia (przelaczane
// skryptem; kotwica z przekierowania, np. #noc, otwiera wlasciwa zakladke,
// ostatnia zakladka pamietana w przegladarce).
static void handleWebRoot() {
  String page;
  page.reserve(20480);
  page += F("<!doctype html><html lang=pl><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Zegar</title><style>"
    "body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#eee;"
    "display:flex;flex-direction:column;align-items:center;padding:16px 16px 32px}"
    "h1{font-size:22px;margin:0 0 2px}"
    "a{display:block;width:100%;max-width:360px;box-sizing:border-box;margin:6px 0;"
    "padding:18px;border-radius:14px;background:#222;color:#eee;text-decoration:none;"
    "font-size:20px;text-align:center;border:2px solid #333}"
    "a.on{background:#1d3b5a;border-color:#3a7bd5}"
    "h2{font-size:16px;color:#aaa;margin:20px 0 6px;font-weight:600;text-align:center}"
    ".row{display:flex;flex-wrap:wrap;gap:8px;width:100%;max-width:360px}"
    ".row a{flex:1 1 40%;margin:0;padding:14px;font-size:17px}"
    ".info{color:#aaa;font-size:14px;max-width:360px;margin:2px 0 8px;text-align:center}"
    ".ok{color:#6c6}.err{color:#e66}"
    "input[type=range]{width:100%;max-width:360px;margin:8px 0 4px}"
    "form{width:100%;max-width:360px;display:flex;gap:8px;margin:4px 0}"
    "input[type=url]{flex:1;min-width:0;padding:12px;border-radius:10px;border:2px solid #333;"
    "background:#222;color:#eee;font-size:15px}"
    "input[type=number]{width:56px;padding:10px;border-radius:10px;border:2px solid #333;"
    "background:#222;color:#eee;font-size:15px}"
    "form.night{flex-wrap:wrap;align-items:center;justify-content:center;color:#aaa}"
    "form.show{flex-direction:column;color:#aaa}"
    ".srow{display:flex;gap:6px;align-items:center}"
    "select{flex:1;min-width:0;padding:10px;border-radius:10px;border:2px solid #333;"
    "background:#222;color:#eee;font-size:15px}"
    "button{padding:12px 16px;border-radius:10px;border:2px solid #3a7bd5;background:#1d3b5a;"
    "color:#eee;font-size:15px}"
    "nav{position:sticky;top:0;z-index:1;display:flex;gap:4px;width:100%;max-width:400px;"
    "background:#111;padding:8px 0;margin-bottom:4px;border-bottom:1px solid #333}"
    "nav button{flex:1;padding:10px 2px;font-size:13px;background:#222;border-color:#333}"
    "nav button.on{background:#1d3b5a;border-color:#3a7bd5}"
    "section{display:none;width:100%;flex-direction:column;align-items:center}"
    "section.on{display:flex}"
    "</style></head><body><h1>Zegar</h1><p class=info>Teraz: ");
  page += getWebModeLabel(static_cast<uint8_t>(_animationMode));
  if (isShowRunning()) {
    page += F(" · Automat włączony");
  }
  if (isScreenOff()) {
    page += isNightClockShown() ? F(" · noc (ciemny zegar)") : F(" · ekran wyłączony");
  }
  page += F("</p><nav>"
    "<button data-t=ekran>Ekran</button><button data-t=automat>Automat</button>"
    "<button data-t=obrazy>Obrazy</button><button data-t=noc>Noc</button>"
    "<button data-t=ustawienia>Ustawienia</button></nav>");

  // --- Ekran: jasnosc i tryby w grupach ---
  page += F("<section id=t-ekran>");
  uint8_t level = getBrightnessLevel();
  page += F("<h2>Jasność: <span id=bv>");
  page += level == 0 ? String("wyłączony") : String(level);
  page += F("</span></h2><input type=range min=0 max=8 value=");
  page += String(level);
  page += F(" oninput=\"bv.textContent=this.value==0?'wyłączony':this.value\""
    " onchange=\"location='/brightness?back=1&v='+this.value\">"
    "<div class=row><a href='/brightness?back=1&v=0'>Wyłącz ekran</a>"
    "<a href='/brightness?back=1&v=8'>Domyślna</a></div>");
  page += F("<span id=tryb></span>");
  for (const WebModeGroup& group : kWebModeGroups) {
    page += F("<h2>");
    page += group.title;
    page += F("</h2><div class=row>");
    for (uint8_t i = 0; i < group.count; i++) {
      appendModeButton(page, static_cast<uint8_t>(group.modes[i]));
    }
    page += F("</div>");
  }
  page += F("<h2>Automat</h2><div class=row>");
  appendModeButton(page, static_cast<uint8_t>(AnimationMode::Show));
  page += F("</div></section>");

  // --- Automat: lista, pasek, przelaczanie na Claude ---
  page += F("<section id=t-automat>");
  appendShowSection(page);
  page += F("<h2>Ekran Claude</h2>");
  page += isClaudeAutoSwitchEnabled()
    ? F("<p class=info>Gdy Claude pracuje, zegar sam przełącza się na jego ekran.</p>"
      "<div class=row><a href='/ai?auto=0'>Nie przełączaj na Claude</a></div>")
    : F("<p class=info>Automatyczne przełączanie na ekran Claude wyłączone.</p>"
      "<div class=row><a href='/ai?auto=1'>Przełączaj na Claude</a></div>");
  page += F("</section>");

  // --- Obrazy: GIF i obrazki ---
  page += F("<section id=t-obrazy>");
  // Wspolne miejsce na partycji FFat: GIF-y (ok. 4 KB na klatke) i obrazki (12 KB).
  uint32_t freeBytes = getGifFreeBytes();
  uint8_t imagesLeft = min<uint32_t>(kImageMax - getImageCount(), freeBytes / kImageBytes);
  page += F("<p class=info>Miejsce na obrazki i GIF-y: wolne ");
  page += String(freeBytes / 1048576.0f, 1);
  page += F(" MB z ");
  page += String(FFat.totalBytes() / 1048576.0f, 1);
  page += F(" MB - zmieści się jeszcze ok. ");
  page += String(freeBytes / 4200);
  page += F(" klatek GIF albo ");
  page += String(imagesLeft);
  page += F(" obrazków (obrazki: ");
  page += String(getImageCount());
  page += '/';
  page += String(kImageMax);
  page += F(", GIF-y: ");
  page += String(getGifCount());
  page += '/';
  page += String(kGifMax);
  page += F(").</p>");
  // Wybor GIF-a.
  bool gifActive = _animationMode == AnimationMode::Gif;
  int8_t selection = getGifSelection();
  page += F("<h2 id=gify>GIF</h2>");
  String gifResult = _webServer.arg("gif");
  if (gifResult == "ok") {
    page += F("<p class='info ok'>GIF wgrany.</p>");
  } else if (gifResult == "blad") {
    page += F("<p class='info err'>Nie wyszło (za dużo GIF-ów, brak miejsca albo zły plik).</p>");
  }
  page += F("<div class=row><a href='/gif?i=random'");
  if (gifActive && selection < 0) {
    page += F(" class=on");
  }
  page += F(">🎲 Losowo</a>");
  for (uint8_t clip = 0; clip < getGifCount(); clip++) {
    page += F("<a href='/gif?i=");
    page += String(clip);
    page += F("'");
    if (gifActive && selection == clip) {
      page += F(" class=on");
    }
    page += F(">");
    page += htmlEscape(getGifName(clip));
    page += F("</a>");
  }
  page += F("</div>");
  if (selection >= 0 && selection < getGifCount()) {
    page += F("<div class=row><a href='/gif?del=");
    page += String(selection);
    page += F("' onclick=\"return confirm('Usunąć ten GIF?')\">🗑 Usuń GIF: ");
    page += htmlEscape(getGifName(selection));
    page += F("</a></div>");
  }

  // Wgrywanie GIF-a: przegladarka sama rozklada GIF (wlasny dekoder - dziala
  // w kazdej przegladarce), przycina do kwadratu, zmniejsza do 64x64, dobiera
  // wspolna palete (do 256 kolorow, median cut) i wysyla gotowy plik .g64.
  page += F("<p class=info id=gs>Nowy GIF (przycięty do kwadratu, 64x64):</p>"
    "<input type=file accept='image/gif' onchange='gifUp(this.files[0])'>"
    "<script>const GIF_FREE=");
  page += String(freeBytes);
  page += F(R"JS(;
function lzw(min,d,size){const out=new Uint8Array(size),clear=1<<min,eoi=clear+1,pre=new Int16Array(4096),suf=new Uint8Array(4096),st=new Uint8Array(4097);
let cs=min+1,mask=(1<<cs)-1,next=eoi+1,old=-1,first=0,pos=0,bits=0,buf=0,i=0;for(let c=0;c<clear;c++){pre[c]=-1;suf[c]=c}
while(pos<size){while(bits<cs){if(i>=d.length)return out;buf|=d[i++]<<bits;bits+=8}let code=buf&mask;buf>>=cs;bits-=cs;
if(code==clear){cs=min+1;mask=(1<<cs)-1;next=eoi+1;old=-1;continue}if(code==eoi)break;
if(old<0){out[pos++]=suf[code];old=code;first=code;continue}const inc=code;let sp=0;
if(code>=next){st[sp++]=first;code=old}while(code>=clear){st[sp++]=suf[code];code=pre[code]}first=suf[code];st[sp++]=first;
while(sp>0&&pos<size)out[pos++]=st[--sp];if(next<4096){pre[next]=old;suf[next]=first;next++;if(next>mask&&cs<12){cs++;mask=(1<<cs)-1}}old=inc}return out}
function decodeGif(b){if(String.fromCharCode(b[0],b[1],b[2])!='GIF')throw 'to nie jest GIF';
const W=b[6]|b[7]<<8,H=b[8]|b[9]<<8,f=b[10];let p=13,gct=null;if(f&128){const n=3<<((f&7)+1);gct=b.subarray(p,p+n);p+=n}
const cv=new Uint8ClampedArray(W*H*4),frames=[];let gce={d:0,t:-1,ms:100};
while(p<b.length){const t=b[p++];if(t==0x3B)break;
if(t==0x21){const l=b[p++];if(l==0xF9){const fl=b[p+1];gce={d:(fl>>2)&7,t:fl&1?b[p+4]:-1,ms:(b[p+2]|b[p+3]<<8)*10}}while(b[p])p+=b[p]+1;p++;continue}
if(t!=0x2C)throw 'uszkodzony GIF';const x=b[p]|b[p+1]<<8,y=b[p+2]|b[p+3]<<8,w=b[p+4]|b[p+5]<<8,h=b[p+6]|b[p+7]<<8,fl=b[p+8];p+=9;
let ct=gct;if(fl&128){const n=3<<((fl&7)+1);ct=b.subarray(p,p+n);p+=n}const min=b[p++],parts=[];let len=0;
while(b[p]){parts.push(b.subarray(p+1,p+1+b[p]));len+=b[p];p+=b[p]+1}p++;const data=new Uint8Array(len);let o=0;for(const s of parts){data.set(s,o);o+=s.length}
const idx=lzw(min,data,w*h),rows=[];if(fl&64){for(let r=0;r<h;r+=8)rows.push(r);for(let r=4;r<h;r+=8)rows.push(r);for(let r=2;r<h;r+=4)rows.push(r);for(let r=1;r<h;r+=2)rows.push(r)}else for(let r=0;r<h;r++)rows.push(r);
const saved=gce.d==3?cv.slice():null;
for(let i=0;i<h;i++){const yy=y+rows[i];if(yy>=H)continue;for(let xx=0;xx<w;xx++){const c=idx[i*w+xx];if(c==gce.t||x+xx>=W||!ct)continue;const q=(yy*W+x+xx)*4;cv[q]=ct[c*3];cv[q+1]=ct[c*3+1];cv[q+2]=ct[c*3+2];cv[q+3]=255}}
frames.push({px:cv.slice(),ms:gce.ms<20?100:gce.ms});
if(gce.d==2){for(let yy=y;yy<y+h&&yy<H;yy++)for(let xx=x;xx<x+w&&xx<W;xx++)cv.fill(0,(yy*W+xx)*4,(yy*W+xx)*4+4)}else if(saved)cv.set(saved);
gce={d:0,t:-1,ms:100}}return {W,H,frames}}
function quant(fr){const ex=new Map();let over=false;
for(const f of fr){for(let i=0;i<f.length;i+=4){const k=f[i]<<16|f[i+1]<<8|f[i+2];if(!ex.has(k)){if(ex.size>=256){over=true;break}ex.set(k,ex.size)}}if(over)break}
if(!over)return {pal:[...ex.keys()],map:k=>ex.get(k)};
const B=new Map();for(const f of fr)for(let i=0;i<f.length;i+=4){const k=(f[i]>>3)<<10|(f[i+1]>>3)<<5|f[i+2]>>3;let e=B.get(k);if(!e){e=[0,0,0,0];B.set(k,e)}e[0]++;e[1]+=f[i];e[2]+=f[i+1];e[3]+=f[i+2]}
const items=[...B].map(([k,e])=>({k,n:e[0],c:[e[1]/e[0],e[2]/e[0],e[3]/e[0]]}));const boxes=[items];
while(boxes.length<256){let bi=-1,best=0,ch=0;boxes.forEach((bx,i)=>{if(bx.length<2)return;for(let c=0;c<3;c++){let mn=1e9,mx=-1;for(const it of bx){const v=it.c[c];if(v<mn)mn=v;if(v>mx)mx=v}if(mx-mn>best){best=mx-mn;bi=i;ch=c}}});
if(bi<0)break;const bx=boxes[bi].sort((u,v)=>u.c[ch]-v.c[ch]);let tot=0;for(const it of bx)tot+=it.n;let acc=0,m=0;for(;m<bx.length-2;m++){acc+=bx[m].n;if(acc>=tot/2)break}boxes.splice(bi,1,bx.slice(0,m+1),bx.slice(m+1))}
const pal=boxes.map(bx=>{let r=0,g=0,bl=0,n=0;for(const it of bx){r+=it.c[0]*it.n;g+=it.c[1]*it.n;bl+=it.c[2]*it.n;n+=it.n}return Math.round(r/n)<<16|Math.round(g/n)<<8|Math.round(bl/n)});
const near=new Map();for(const it of items){let bd=1e9,bj=0;for(let j=0;j<pal.length;j++){const q=pal[j],dr=(q>>16)-it.c[0],dg=((q>>8)&255)-it.c[1],db=(q&255)-it.c[2],dd=2*dr*dr+4*dg*dg+3*db*db;if(dd<bd){bd=dd;bj=j}}near.set(it.k,bj)}
return {pal,map:k=>near.get((k>>19)<<10|((k>>11)&31)<<5|((k>>3)&31))}}
async function gifPack(file){const g=decodeGif(new Uint8Array(await file.arrayBuffer())),fr=g.frames;
if(!fr.length)throw 'brak klatek';if(fr.length>600)throw 'za dużo klatek ('+fr.length+', najwyżej 600)';
const c=document.createElement('canvas');c.width=g.W;c.height=g.H;const cx=c.getContext('2d',{willReadFrequently:true});
const o=document.createElement('canvas');o.width=o.height=64;const ox=o.getContext('2d',{willReadFrequently:true});ox.imageSmoothingQuality='high';const s=Math.min(g.W,g.H);
const px=fr.map(f=>{cx.putImageData(new ImageData(f.px,g.W,g.H),0,0);ox.fillStyle='#000';ox.fillRect(0,0,64,64);ox.drawImage(c,(g.W-s)/2,(g.H-s)/2,s,s,0,0,64,64);return ox.getImageData(0,0,64,64).data});
const q=quant(px),n=px.length,P=q.pal.length,size=40+2*n+3*P+4096*n,out=new Uint8Array(size),dv=new DataView(out.buffer);
out.set([71,54,52,1]);dv.setUint16(4,n,true);dv.setUint16(6,P,true);let name=file.name.replace(/\.gif$/i,'');const te=new TextEncoder();
while(te.encode(name).length>31)name=name.slice(0,-1);out.set(te.encode(name),8);let p=40;
for(const f of fr){dv.setUint16(p,Math.min(65535,f.ms),true);p+=2}for(const k of q.pal){out[p++]=k>>16;out[p++]=(k>>8)&255;out[p++]=k&255}
for(const f of px)for(let i=0;i<f.length;i+=4)out[p++]=q.map(f[i]<<16|f[i+1]<<8|f[i+2]);return out}
async function gifUp(file){if(!file)return;const st=document.getElementById('gs');
try{st.textContent='Przygotowuję GIF...';await new Promise(r=>setTimeout(r,30));const out=await gifPack(file);
if(out.length>GIF_FREE-65536)throw 'za mało miejsca ('+Math.round(out.length/1024)+' KB, wolne '+Math.round(GIF_FREE/1024)+' KB)';
st.textContent='Wysyłam ('+Math.round(out.length/1024)+' KB)...';const fd=new FormData();fd.append('f',new Blob([out]),'gif.g64');
const r=await fetch('/gifup',{method:'POST',body:fd});location='/?gif='+(r.ok?'ok':'blad')+'#gify'}catch(e){st.textContent='Nie wyszło: '+e}}
</script>)JS");

  // Obrazki: wybor, usuwanie i wgrywanie (przegladarka przycina do kwadratu
  // i zmniejsza do 64x64, wysyla RGB jako hex na POST /image).
  bool imageActive = _animationMode == AnimationMode::Image;
  int8_t image = getImageSelection();
  page += F("<h2 id=obrazki>Obrazek</h2>");
  String imageResult = _webServer.arg("obrazek");
  if (imageResult == "ok") {
    page += F("<p class='info ok'>Wgrane.</p>");
  } else if (imageResult == "blad") {
    page += F("<p class='info err'>Nie wyszło (za dużo obrazków albo błąd zapisu).</p>");
  }
  page += F("<div class=row>");
  for (uint8_t index = 0; index < getImageCount(); index++) {
    page += F("<a href='/image?i=");
    page += String(index);
    page += F("'");
    if (imageActive && image == index) {
      page += F(" class=on");
    }
    page += F(">");
    page += String(index + 1);
    page += F("</a>");
  }
  page += F("</div>");
  if (getImageCount() > 0) {
    page += F("<div class=row><a href='/image?back=1&del=");
    page += String(image);
    page += F("' onclick=\"return confirm('Usunąć obrazek ");
    page += String(image + 1);
    page += F("?')\">🗑 Usuń obrazek ");
    page += String(image + 1);
    page += F("</a></div>");
  }
  page += F("<p class=info>Nowy obrazek (zostanie przycięty do kwadratu):</p>"
    "<input type=file accept='image/*' onchange='up(this.files[0])'>"
    "<script>async function up(f){if(!f)return;"
    "const b=await createImageBitmap(f),s=Math.min(b.width,b.height),"
    "c=document.createElement('canvas');c.width=c.height=64;"
    "const x=c.getContext('2d');x.imageSmoothingQuality='high';"
    "x.drawImage(b,(b.width-s)/2,(b.height-s)/2,s,s,0,0,64,64);"
    "const d=x.getImageData(0,0,64,64).data;let h='';"
    "for(let i=0;i<d.length;i+=4)for(let k=0;k<3;k++)h+=d[i+k].toString(16).padStart(2,'0');"
    "const r=await fetch('/image',{method:'POST',headers:{'Content-Type':'text/plain'},body:h});"
    "location='/?obrazek='+(r.ok?'ok':'blad')+'#obrazki'}</script>");
  page += F("</section>");

  // --- Noc ---
  page += F("<section id=t-noc>");
  page += F("<h2 id=noc>Pora nocna</h2><p class=info>");
  if (isNightClockShown()) {
    page += F("Teraz pora nocna - ciemny zegar.");
  } else if (isNightOffTime() && !isNightWoken() && !isPcActivityRecent()) {
    page += F("Teraz pora nocna - ekran wyłączony.");
  } else if (isNightOffTime() && isNightWoken()) {
    page += F("Pora nocna, ekran włączony do rana.");
  } else if (isNightOffTime()) {
    page += F("Pora nocna, ekran włączony - komputer jest aktywny.");
  } else if (isDarkNow() && isNightDimEnabled()) {
    page += F("Po zmroku - ekran przyciemniony.");
  } else {
    page += F("Dzień.");
  }
  page += F("</p>");
  if (isNightOffTime() && !isNightWoken() && !isPcActivityRecent()) {
    page += F("<div class=row><a href='/night?wake=1'>💡 Włącz do rana</a></div>");
  }
  page += F("<form class=night action=/night><input type=hidden name=set value=1>"
    "Wyłącz od <input type=number name=s min=0 max=23 value=");
  page += String(getNightOffStart());
  page += F("> do <input type=number name=e min=0 max=23 value=");
  page += String(getNightOffEnd());
  page += F("><label><input type=checkbox name=dim value=1");
  if (isNightDimEnabled()) {
    page += F(" checked");
  }
  page += F("> przyciemniaj po zmroku</label><label>W nocy: <select name=nc>");
  static const char* const kNightClockNames[] {"czarny ekran", "zegar bardzo ciemny",
    "zegar ciemny", "zegar jaśniejszy"};
  for (uint8_t level = 0; level < 4; level++) {
    page += F("<option value=");
    page += String(level);
    if (level == getNightClockLevel()) {
      page += F(" selected");
    }
    page += F(">");
    page += kNightClockNames[level];
    page += F("</option>");
  }
  page += F("</select></label><button>Zapisz</button></form>"
    "<p class=info>Te same godziny (np. 0 i 0) = bez wyłączania.</p></section>");

  // --- Ustawienia: liczniki i stan ---
  page += F("<section id=t-ustawienia>");
  // Liczniki z Google (web_counters.cpp) - adresu nie pokazujemy.
  page += F("<h2 id=liczniki>Liczniki (Gmail, MangaDex, YouTube)</h2>");
  String countersResult = _webServer.arg("liczniki");
  if (countersResult == "ok") {
    page += F("<p class='info ok'>Zapisane - zegar zaraz zapyta Google.</p>");
  } else if (countersResult == "blad") {
    page += F("<p class='info err'>To nie jest adres https://script.google.com/...</p>");
  }
  if (hasCountersUrl()) {
    uint32_t ago = getCountersFetchedAgoSeconds();
    page += F("<p class=info>Adres ustawiony. ");
    if (ago == UINT32_MAX) {
      int status = getCountersLastStatus();
      page += status == 0 ? String(F("Jeszcze nie pobrano."))
        : String(F("Błąd pobierania: ")) + status;
    } else {
      page += F("Ostatnio ");
      page += String(ago / 60);
      page += F(" min temu: maile ");
      page += String(_mailUnread);
      page += F(", rozdziały ");
      page += _mangaUnread >= 0 ? String(_mangaUnread) : String("?");
      page += F(".");
    }
    page += F("</p><div class=row><a href='/counters?clear=1'"
      " onclick=\"return confirm('Usunąć adres?')\">Usuń adres</a></div>");
  } else {
    page += F("<p class=info>Wklej adres /exec skryptu zegar_liczniki.gs:</p>");
  }
  page += F("<form method=post action=/counters><input type=url name=url required"
    " placeholder='https://script.google.com/macros/s/.../exec'>"
    "<button>Zapisz</button></form>");
  page += F("<h2>Stan zegara</h2><p class=info>Wi-Fi ");
  page += String(WiFi.RSSI());
  page += F(" dBm · od startu ");
  page += String(millis() / 3600000UL);
  page += F(" h ");
  page += String((millis() / 60000UL) % 60);
  page += F(" min · wolna pamięć ");
  page += String(ESP.getFreeHeap() / 1024);
  page += F(" KB</p><div class=row><a href='/info'>Szczegóły (/info)</a></div></section>");

  // Zakladki: kotwica (#noc, #automat...) albo ostatnio otwarta, domyslnie Ekran.
  page += F("<script>"
    "const tabs={tryb:'ekran',automat:'automat',obrazki:'obrazy',gify:'obrazy',noc:'noc',"
    "liczniki:'ustawienia'};"
    "function show(t){document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id=='t-'+t));"
    "document.querySelectorAll('nav button').forEach(b=>b.classList.toggle('on',b.dataset.t==t));"
    "try{localStorage.setItem('tab',t)}catch(e){}}"
    "document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{show(b.dataset.t);"
    "history.replaceState(null,'','/')});"
    "let t=tabs[location.hash.slice(1)]||location.hash.slice(1);"
    "if(!document.getElementById('t-'+t)){try{t=localStorage.getItem('tab')}catch(e){t=null}}"
    "show(document.getElementById('t-'+t)?t:'ekran');"
    "</script></body></html>");
  _webServer.send(200, "text/html; charset=utf-8", page);
}

static void handleWebMode() {
  String id = _webServer.arg("m");
  for (const WebMode& mode : kWebModes) {
    if (id == mode.id) {
      // Przy Automacie kazdy inny wybor go konczy (tez tryb, ktory akurat pokazuje).
      if (mode.mode == AnimationMode::Show ? !isShowRunning()
        : mode.mode != _animationMode || isShowRunning()) {
        selectAnimation(mode.mode);
      }
      _claudeAutoShown = false; // wybrany recznie (tez Claude) - zostaje
      _webServer.sendHeader("Location", "/");
      _webServer.send(303, "text/plain", "OK");
      return;
    }
  }
  _webServer.send(400, "text/plain; charset=utf-8", "Nieznany tryb");
}

static void handleWebGif() {
  if (_webServer.hasArg("del")) {
    deleteGif(static_cast<uint8_t>(_webServer.arg("del").toInt()));
    _webServer.sendHeader("Location", "/#gify");
    _webServer.send(303, "text/plain", "OK");
    return;
  }
  String value = _webServer.arg("i");
  int8_t selection = -1;
  if (value != "random") {
    char* end = nullptr;
    long parsed = strtol(value.c_str(), &end, 10);
    if (value.length() == 0 || end == nullptr || *end != '\0'
      || parsed < 0 || parsed >= getGifCount()) {
      _webServer.send(400, "text/plain; charset=utf-8", "Nieznany GIF");
      return;
    }
    selection = static_cast<int8_t>(parsed);
  }
  setGifSelection(selection);
  _webServer.sendHeader("Location", "/#gify");
  _webServer.send(303, "text/plain", "OK");
}

// POST /gifup (multipart, plik .g64 ze strony) - zapis strumieniem na FFat
// (gif_animation.cpp), bez trzymania calego pliku w RAM.
static void handleWebGifUploadData() {
  HTTPUpload& upload = _webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    beginGifUpload();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    writeGifUpload(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    abortGifUpload();
  }
}

static void handleWebGifUploadDone() {
  bool ok = finishGifUpload();
  _webServer.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleWebAi() {
  if (_webServer.hasArg("auto")) {
    // Automatyczne przelaczanie na panel Claude (clock_ai_status.cpp), ze strony.
    setClaudeAutoSwitch(_webServer.arg("auto") != "0");
    _webServer.sendHeader("Location", "/#automat");
    _webServer.send(303, "text/plain", "OK");
    return;
  }
  if (_webServer.hasArg("s")) {
    if (!setAiStateByName(_webServer.arg("s"))) {
      _webServer.send(400, "text/plain; charset=utf-8", "Nieznany stan (work/wait/done/idle)");
      return;
    }
  }
  String body = String("{\"state\":\"") + getAiStateName() + "\"}";
  _webServer.send(200, "application/json", body);
}

static bool readClaudeLimitArgs(const char* usedArg, const char* resetArg,
  ClaudeLimit& limit) {
  if (!_webServer.hasArg(usedArg)) {
    return false;
  }
  limit.valid = true;
  limit.usedPercent = _webServer.arg(usedArg).toFloat();
  limit.resetsAt = strtoul(_webServer.arg(resetArg).c_str(), nullptr, 10);
  return true;
}

static void appendClaudeLimitJson(String& body, const char* name, const ClaudeLimit& limit) {
  body += F(",\"");
  body += name;
  body += F("\":");
  if (!limit.valid) {
    body += F("null");
    return;
  }
  body += F("{\"used\":");
  body += String(limit.usedPercent, 1);
  body += F(",\"resets_at\":");
  body += String(limit.resetsAt);
  body += F("}");
}

static void handleWebClaude() {
  bool updated = readClaudeLimitArgs("h", "hr", _claude.fiveHour);
  updated |= readClaudeLimitArgs("w", "wr", _claude.sevenDay);
  if (_webServer.hasArg("c")) {
    _claude.contextValid = true;
    _claude.contextIsTokens = false;
    _claude.contextUsedPercent = _webServer.arg("c").toFloat();
    updated = true;
  } else if (_webServer.hasArg("ct")) {
    _claude.contextValid = true;
    _claude.contextIsTokens = true;
    _claude.contextTokens = strtoul(_webServer.arg("ct").c_str(), nullptr, 10);
    updated = true;
  }
  if (_webServer.hasArg("s")) {
    setAiStateByName(_webServer.arg("s"));
  }
  if (_webServer.hasArg("usd")) {
    _claude.costValid = true;
    _claude.costUsd = _webServer.arg("usd").toFloat();
    updated = true;
  }
  if (_webServer.hasArg("m")) {
    // Bez cudzyslowow i backslashy - nazwa trafia tez do odpowiedzi JSON.
    String model = _webServer.arg("m");
    size_t length = 0;
    for (size_t i = 0; i < model.length() && length < sizeof(_claude.model) - 1; i++) {
      char c = model[i];
      if (c >= ' ' && c <= '~' && c != '"' && c != '\\') {
        _claude.model[length++] = c;
      }
    }
    _claude.model[length] = '\0';
    updated = true;
  }
  if (updated) {
    _claude.updatedAtMs = millis() | 1; // 0 = jeszcze nic nie przyszlo
    saveClaudeUsage();
  }

  String body = String("{\"state\":\"") + getAiStateName() + "\",\"model\":\""
    + _claude.model + "\"";
  appendClaudeLimitJson(body, "five_hour", _claude.fiveHour);
  appendClaudeLimitJson(body, "seven_day", _claude.sevenDay);
  body += F(",\"context_used\":");
  body += _claude.contextValid && !_claude.contextIsTokens
    ? String(_claude.contextUsedPercent, 1) : String("null");
  body += F(",\"context_tokens\":");
  body += _claude.contextValid && _claude.contextIsTokens
    ? String(_claude.contextTokens) : String("null");
  body += F(",\"cost_usd\":");
  body += _claude.costValid ? String(_claude.costUsd, 2) : String("null");
  body += F("}");
  _webServer.send(200, "application/json", body);
}

static void handleWebMessage() {
  long seconds = _webServer.hasArg("s") ? _webServer.arg("s").toInt() : 0;
  seconds = constrain(seconds, 0L, 600L);
  float hue = _webServer.hasArg("c") ? _webServer.arg("c").toFloat() / 360.0f : 0.13f;
  hue -= floorf(hue);
  showMessage(_webServer.arg("t"), static_cast<uint32_t>(seconds), hue);
  _webServer.send(200, "application/json",
    isMessageActive() ? "{\"shown\":true}" : "{\"shown\":false}");
}

static uint32_t _wifiDisconnects{0};
static uint8_t _wifiLastDisconnectReason{0};

// Licznik zerwan Wi-Fi do /info .
void watchWifiDisconnects() {
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    _wifiDisconnects++;
    _wifiLastDisconnectReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

static const char* getResetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "wlaczenie zasilania";
    case ESP_RST_EXT: return "przycisk reset";
    case ESP_RST_SW: return "restart programowy (np. po OTA)";
    case ESP_RST_PANIC: return "BLAD PROGRAMU (panic)";
    case ESP_RST_INT_WDT: return "watchdog przerwan";
    case ESP_RST_TASK_WDT: return "watchdog zadania (zawieszenie)";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "SPADEK NAPIECIA (brownout)";
    default: return "inny";
  }
}

static void handleWebInfo() {
  char text[320];
  snprintf(text, sizeof(text),
    "{\"uptime_s\":%lu,\"reset\":\"%s\",\"mode\":%u,\"rssi_dbm\":%d,"
    "\"wifi_disconnects\":%lu,\"last_disconnect_reason\":%u,\"heap_free\":%u,"
    "\"heap_min\":%u,\"panel\":\"%s\"}",
    static_cast<unsigned long>(millis() / 1000), getResetReasonName(),
    static_cast<unsigned>(_animationMode), WiFi.RSSI(),
    static_cast<unsigned long>(_wifiDisconnects), _wifiLastDisconnectReason,
    static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
    isPanelActive() ? "on" : "off");
  // Na koncu tekstu "}" - dopinamy stan trybu nocnego.
  String body(text);
  body.remove(body.length() - 1);
  body += F(",\"night\":");
  body += getNightStatusJson();
  body += F("}");
  _webServer.send(200, "application/json", body);
}

// Panel: stan (on / off = wylaczony po 10 min bez Wi-Fi albo nieudany start DMA),
// strojenie /panel?clkphase=0|1&lat=0..8&mhz=8|16|20 (lat=0 = automatycznie) -
// zapis i restart (panel.cpp).
static void handleWebPanel() {
  if (_webServer.hasArg("clkphase") || _webServer.hasArg("lat") || _webServer.hasArg("mhz")) {
    _webServer.send(200, "text/plain; charset=utf-8", "Restart...");
    setPanelTuningAndRestart(
      _webServer.hasArg("clkphase") ? _webServer.arg("clkphase").toInt() : -1,
      _webServer.hasArg("lat") ? _webServer.arg("lat").toInt() : -1,
      _webServer.hasArg("mhz") ? _webServer.arg("mhz").toInt() : -1);
    return;
  }
  _webServer.send(200, "application/json",
    String("{\"panel\":\"") + (isPanelActive() ? "on" : "off") + "\""
    + getPanelTuningJson() + "}");
}

static void handleWebManga() {
  if (_webServer.hasArg("n")) {
    long count = _webServer.arg("n").toInt();
    _mangaUnread = static_cast<int16_t>(count < 0 ? 0 : (count > 999 ? 999 : count));
    _mangaUpdatedAtMs = millis();
  }
  _webServer.send(200, "application/json", String("{\"unread\":") + _mangaUnread + "}");
}

// Ekran "Teraz grane" (mode_music.cpp): opis utworu albo off=1.
static void handleWebNowPlaying() {
  if (_webServer.hasArg("idle")) {
    notePcIdleSeconds(strtoul(_webServer.arg("idle").c_str(), nullptr, 10));
  }
  if (_webServer.hasArg("off")) {
    clearNowPlaying();
  } else if (_webServer.hasArg("t") || _webServer.hasArg("a")) {
    updateNowPlaying(_webServer.arg("a"), _webServer.arg("t"),
      strtoul(_webServer.arg("p").c_str(), nullptr, 10),
      strtoul(_webServer.arg("d").c_str(), nullptr, 10),
      _webServer.arg("s") != "0");
  }
  _webServer.send(200, "application/json", getNowPlayingJson());
}

// Ekran "Komputer" (mode_pc.cpp): obciazenie z muzyka_zegar.ps1 co 2 s.
static void handleWebPc() {
  if (_webServer.hasArg("c")) {
    updatePcStats(static_cast<uint8_t>(constrain(_webServer.arg("c").toInt(), 0L, 100L)),
      static_cast<uint8_t>(constrain(_webServer.arg("g").toInt(), 0L, 100L)),
      static_cast<uint8_t>(constrain(_webServer.arg("r").toInt(), 0L, 100L)),
      strtoul(_webServer.arg("ru").c_str(), nullptr, 10),
      strtoul(_webServer.arg("rt").c_str(), nullptr, 10),
      strtoul(_webServer.arg("v").c_str(), nullptr, 10),
      strtoul(_webServer.arg("vt").c_str(), nullptr, 10));
  }
  _webServer.send(200, "application/json", getPcStatsJson());
}

static void handleWebRates() {
  _webServer.send(200, "application/json", getRatesJson());
}

// Okladka 48x48 RGB jako hex w tresci POST (text/plain).
static void handleWebCover() {
  bool ok = setNowPlayingCover(_webServer.arg("plain"));
  _webServer.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Obrazki (mode_image.cpp): POST = nowy (hex z przegladarki), ?i=N wybor,
// ?del=N usuniecie; back=1 - powrot na strone.
static void handleWebImage() {
  if (_webServer.method() == HTTP_POST) {
    bool ok = addImage(_webServer.arg("plain"));
    _webServer.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return;
  }
  if (_webServer.hasArg("del")) {
    deleteImage(static_cast<uint8_t>(_webServer.arg("del").toInt()));
  } else if (_webServer.hasArg("i")) {
    setImageSelection(static_cast<int8_t>(_webServer.arg("i").toInt()));
  }
  if (_webServer.hasArg("del") || _webServer.hasArg("i") || _webServer.hasArg("back")) {
    _webServer.sendHeader("Location", "/#obrazki");
    _webServer.send(303, "text/plain", "OK");
    return;
  }
  _webServer.send(200, "application/json", String("{\"count\":") + getImageCount()
    + ",\"selection\":" + getImageSelection() + "}");
}

// Adres Apps Script dla licznikow (web_counters.cpp): POST url=..., GET clear=1.
static void handleWebCounters() {
  if (_webServer.method() == HTTP_POST || _webServer.hasArg("clear")) {
    bool ok = setCountersUrl(_webServer.hasArg("clear") ? String() : _webServer.arg("url"));
    _webServer.sendHeader("Location", ok ? "/?liczniki=ok#liczniki" : "/?liczniki=blad#liczniki");
    _webServer.send(303, "text/plain", "OK");
    return;
  }
  _webServer.send(200, "application/json", getCountersStatusJson());
}

// Tryb nocny (brightness.cpp): set=1&s=23&e=6&dim=1&nc=0..3 albo wake=1.
// Automat (mode_show.cpp): e=<tryb.wariant>&t=<s> po kolei, run=1 - wlacz,
// bar=0|1|2 - pasek powiadomien (notify_bar.cpp).
static void handleWebShow() {
  String values[kShowMaxEntries + 1];
  uint16_t seconds[kShowMaxEntries + 1];
  uint8_t count = 0;
  for (int arg = 0; arg < _webServer.args() && count <= kShowMaxEntries; arg++) {
    if (_webServer.argName(arg) == "e") {
      values[count] = _webServer.arg(arg);
      seconds[count] = 60;
      count++;
    } else if (_webServer.argName(arg) == "t" && count > 0) {
      seconds[count - 1] = static_cast<uint16_t>(constrain(_webServer.arg(arg).toInt(), 0L, 65535L));
    }
  }
  if (_webServer.hasArg("e")) {
    setShowEntriesFromWeb(values, seconds, count);
  }
  if (_webServer.hasArg("bar")) {
    setNotifyBarMode(static_cast<uint8_t>(constrain(_webServer.arg("bar").toInt(), 0L, 2L)));
  }
  if (_webServer.hasArg("barfit")) {
    setNotifyBarFit(_webServer.arg("barfit") == "1");
  }
  if (_webServer.hasArg("run") && getShowCount() > 0 && !isShowRunning()) {
    selectAnimation(AnimationMode::Show);
  }
  _webServer.sendHeader("Location", "/#automat");
  _webServer.send(303, "text/plain", "OK");
}

static void handleWebNight() {
  if (_webServer.hasArg("wake")) {
    wakeUntilMorning();
  } else if (_webServer.hasArg("set")) {
    long start = _webServer.arg("s").toInt();
    long end = _webServer.arg("e").toInt();
    setNightMode(static_cast<uint8_t>(constrain(start, 0L, 23L)),
      static_cast<uint8_t>(constrain(end, 0L, 23L)), _webServer.hasArg("dim"),
      static_cast<uint8_t>(constrain(_webServer.arg("nc").toInt(), 0L, 3L)));
  }
  _webServer.sendHeader("Location", "/#noc");
  _webServer.send(303, "text/plain", "OK");
}

static void handleWebMail() {
  if (_webServer.hasArg("n")) {
    long count = _webServer.arg("n").toInt();
    _mailUnread = static_cast<int16_t>(count < 0 ? 0 : (count > 999 ? 999 : count));
    _mailUpdatedAtMs = millis();
  }
  _webServer.send(200, "application/json", String("{\"unread\":") + _mailUnread + "}");
}

// Recznie / do testu: liczba kanalow YouTube na zywo (zwykle z Apps Script).
static void handleWebLive() {
  if (_webServer.hasArg("n")) {
    long count = _webServer.arg("n").toInt();
    _liveCount = static_cast<int16_t>(count < 0 ? 0 : (count > 999 ? 999 : count));
    _liveUpdatedAtMs = millis();
  }
  _webServer.send(200, "application/json", String("{\"live\":") + _liveCount + "}");
}

// Nieprzeczytane powiadomienia comix.to (muzyka_zegar.ps1 na komputerze).
static void handleWebComix() {
  if (_webServer.hasArg("n")) {
    long count = _webServer.arg("n").toInt();
    _comixUnread = static_cast<int16_t>(count < 0 ? 0 : (count > 999 ? 999 : count));
    _comixUpdatedAtMs = millis();
  }
  _webServer.send(200, "application/json", String("{\"comix\":") + _comixUnread + "}");
}

static void handleWebBrightness() {
  if (_webServer.hasArg("v")) {
    long level = _webServer.arg("v").toInt();
    setBrightnessLevel(static_cast<uint8_t>(level < 0 ? 0 : (level > 8 ? 8 : level)));
  }
  if (_webServer.hasArg("back")) {
    _webServer.sendHeader("Location", "/");
    _webServer.send(303, "text/plain", "OK");
    return;
  }
  _webServer.send(200, "application/json",
    String("{\"level\":") + getBrightnessLevel() + "}");
}

void updateWebControl() {
  if (!_webStarted) {
    if (WiFi.status() != WL_CONNECTED) {
      return;
    }
    _webServer.on("/", HTTP_GET, handleWebRoot);
    _webServer.on("/mode", HTTP_GET, handleWebMode);
    _webServer.on("/gif", HTTP_GET, handleWebGif);
    _webServer.on("/gifup", HTTP_POST, handleWebGifUploadDone, handleWebGifUploadData);
    _webServer.on("/ai", handleWebAi); // GET lub POST (np. hooki typu http)
    _webServer.on("/claude", handleWebClaude);
    _webServer.on("/msg", handleWebMessage);
    _webServer.on("/info", HTTP_GET, handleWebInfo);
    _webServer.on("/brightness", HTTP_GET, handleWebBrightness);
    _webServer.on("/night", HTTP_GET, handleWebNight);
    _webServer.on("/show", HTTP_GET, handleWebShow);
    _webServer.on("/panel", HTTP_GET, handleWebPanel);
    _webServer.on("/manga", HTTP_GET, handleWebManga);
    _webServer.on("/mail", HTTP_GET, handleWebMail);
    _webServer.on("/live", HTTP_GET, handleWebLive);
    _webServer.on("/comix", HTTP_GET, handleWebComix);
    _webServer.on("/counters", handleWebCounters); // POST zapis adresu, GET stan
    _webServer.on("/np", HTTP_GET, handleWebNowPlaying);
    _webServer.on("/cover", HTTP_POST, handleWebCover);
    _webServer.on("/image", handleWebImage); // GET wybor/usuwanie, POST nowy
    _webServer.on("/pc", HTTP_GET, handleWebPc);
    _webServer.on("/rates", HTTP_GET, handleWebRates);
    _webServer.begin();
    MDNS.addService("http", "tcp", 80); // mDNS uruchamia juz ArduinoOTA
    _webStarted = true;
    Serial.printf("Sterowanie: http://%s.local  (IP %s)\n",
      kDeviceHostname, WiFi.localIP().toString().c_str());
    return;
  }
  _webServer.handleClient();
}
