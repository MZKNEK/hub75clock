// Obudowa tylna dla panelu LED P2 64x64 (128x128 mm) + MatrixPortal S3.
// Generuje STL:  node obudowa.js   (wymaga @jscad/modeling)
//
// Uklad osi (rama): x - w prawo, y - w gore (panel wisi), z - od tylnej
// krawedzi ramy (z = 0, na stole drukarki) w strone frontu panelu.

const {primitives, booleans, transforms, hulls, geometries} = require('@jscad/modeling')
const {cuboid, cylinder, cylinderElliptic, polygon} = primitives
const {union, subtract} = booleans
const {translate, rotateX, rotateZ} = transforms
const {extrudeLinear} = require('@jscad/modeling').extrusions
const fs = require('fs')
const path = require('path')

// ------------------------------------------------------------------ parametry
const P = {
  panel: 128.0,          // bok panelu [mm]
  panelThickness: 14.0,  // grubosc panelu
  clearance: 0.2,        // luz panel <-> rama (na strone); front panelu 128, tylna ramka 127.6
  wall: 2.4,             // grubosc scianek
  extension: 28.0,       // o ile scianki wystaja za tyl panelu (bylo 38 - po przymiarce
                         // sterownika z tasma uzytkownik obnizyl o 1 cm)
  frontGap: 0.5,         // rama konczy sie tyle przed frontem panelu

  rim: 12.0,             // szerokosc tylnej ramki panelu
  flangeWidth: 11.5,     // kolnierz oparty na ramce panelu
  flangeThickness: 3.0,

  // mosiezne gniazda M3 w tylnej ramce panelu (zmierzone): prostokat
  // 112.3 x 90.0 mm (srodek do srodka), wysrodkowany na panelu.
  insertSpanX: 112.3,
  insertSpanY: 90.0,
  screwHole: 3.9,        // przelot M3 z zapasem +-0.4 mm na tolerancje pomiaru
  screwCounterbore: 7.5, // kieszen na leb + srubokret

  // sruby pokrywy (M3, wkrecane w PLA)
  bossSize: 9.0,
  bossDepth: 12.0,
  bossHole: 2.6,

  // wyjscie USB w dolnej sciance (otwarte od tylu) - jedno, po stronie USB
  // sterownika (usbOnRight nizej; od tylu zegara wypada ono po LEWEJ, bo widok
  // od tylu jest lustrzany wzgledem widoku od srodka pokrywy).
  usbWidth: 16.0,
  usbDepth: 22.0,
  usbFromBoardEdge: 8.26, // srodek gniazda USB od dluzszej krawedzi plytki (projekt PCB Adafruit)

  // pokrywa
  coverThickness: 3.0,
  // dodatkowa grubosc pokrywy na ZEWNATRZ (od sciany): lby srub pokrywy siedza
  // o tyle glebiej (po pierwszym wydruku wystawaly), srodek bez zmian - rama
  // i slupki sterownika pasuja jak wczesniej. Zegar odstaje o tyle od sciany.
  coverExtra: 2.0,
  lipHeight: 3.0,
  lipWidth: 1.6,
  lipClearance: 0.3,
  keyholeSpacing: 80.0,
  keyholeY: 38.0,        // od srodka w gore
  keyholeHead: 8.5,
  keyholeSlot: 4.2,
  keyholeLength: 9.0,

  // sterownik (MatrixPortal S3: 63.5 x 44.45 mm) na 4 slupkach w otworach
  // montazowych 2.5 mm plytki. boardFix:
  //   'screw' - sruby M2.5 x 8 (albo M2) wkrecane w PLA (M3 NIE przejdzie przez otwor),
  //   'peg'   - kolki 2.3 mm przechodza przez otwory; wystajace koncowki
  //             splaszcza sie grotem lutownicy (bez srub, trzyma na stale).
  // (Wczesniejsze sanki kolidowaly z elementami na plytce, a ogranicznik na
  // dole zaslanial przyciski UP/DOWN/RST, ktore sa na krawedzi z USB.)
  // Otwory wg projektu PCB Adafruit: [od krotszej krawedzi z USB i przyciskami,
  // od dluzszej krawedzi blizszej USB]. Klon sprawdzic test_sterownik.stl.
  boardWidth: 44.45,
  boardHeight: 63.5,
  boardHoles: [[7.62, 15.875], [7.62, 35.56], [48.26, 15.875], [48.26, 35.56]],
  // Patrzac od srodka pokrywy plytka lezy USB w dol i USB jest przy PRAWYM rogu,
  // wiec 'od dluzszej krawedzi przy USB' liczy sie od prawej. (Pierwszy test
  // mial to odbite lustrzanie - kolki i USB po zlej stronie.)
  holesFromLeft: false,
  usbOnRight: true,      // wyciecie USB w ramie i pokrywie + znacznik w tescie
  // Srodek plytki przesuniety w bok, OD strony USB: centralnie pod sterownikiem
  // jest zlacze zasilania panelu. 28 mm = lewa krawedz plytki 2,5 mm od kolnierza.
  boardOffsetX: -28.0,
  boardFix: 'screw',
  pcb: 1.6,
  boardAboveCover: 11.0, // PCB nad pokrywa: gniazdo od spodu ma 10 mm (+1 mm luzu)
  boardSockets: 10.0,    // gniazdo od strony pokrywy (zmierzone)
  boardAboveBottom: 12.0,// dolna krawedz plytki nad dolna scianka (wtyk USB, przyciski)
  standoffDiameter: 5.0,
  standoffBaseDiameter: 8.0, // stozek 3 mm u podstawy - mocniejszy slupek
  standoffHole: 2.1,     // otwor pod M2.5 wkrecana w PLA
  standoffHoleDepth: 9.0,
  pegDiameter: 2.3,      // kolek przez otwor 2.5 mm
  pegAbove: 2.5,         // tyle wystaje nad plytke (do splaszczenia lutownica)
}

const SEG = 48
const inner = P.panel + 2 * P.clearance
const outer = inner + 2 * P.wall
const panelBackZ = P.extension
const frontZ = P.extension + P.panelThickness - P.frontGap
const flangeZ0 = panelBackZ - P.flangeThickness
const gussetZ0 = flangeZ0 - P.flangeWidth   // podparcie 45 st. - druk bez podpor

const box = (x0, y0, z0, x1, y1, z1) => cuboid({
  center: [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2],
  size: [x1 - x0, y1 - y0, z1 - z0],
})
const cyl = (x, y, z0, z1, d) => cylinder({
  center: [x, y, (z0 + z1) / 2], height: z1 - z0, radius: d / 2, segments: SEG,
})

// pozycje mosieznych gniazd panelu (wzgledem srodka): 4 zmierzone + te same
// obrocone o 90 st. - panel mozna przykrecic w dowolnej orientacji (8 otworow).
function insertPositions() {
  const x = P.insertSpanX / 2
  const y = P.insertSpanY / 2
  const list = []
  for (const sx of [-1, 1]) {
    for (const sy of [-1, 1]) {
      list.push([sx * x, sy * y], [sx * y, sy * x])
    }
  }
  return list
}

function usbCentersX() {
  const x = P.boardWidth / 2 - P.usbFromBoardEdge
  return [P.boardOffsetX + (P.usbOnRight ? x : -x)]
}

// srodki otworow sterownika w ukladzie pokrywy (x, y)
function boardHoleCenters() {
  const yb = -inner / 2 + P.boardAboveBottom
  const half = P.boardWidth / 2
  return P.boardHoles.map(([along, across]) =>
    [P.boardOffsetX + (P.holesFromLeft ? -half + across : half - across), yb + along])
}

// slupki od wysokosci z0 (powierzchnia pokrywy) do spodu plytki; sink = ile
// wchodza w plyte pod spodem (wieksze = mniej problemow z siatka przy unii)
function standoffs(z0, sink = 0.01) {
  const top = z0 + P.boardAboveCover
  const b = P.standoffBaseDiameter / 2
  const d = P.standoffDiameter / 2
  return boardHoleCenters().map(([x, y]) => {
    const post = union(
      cyl(x, y, z0 - sink, top, P.standoffDiameter),
      cylinderElliptic({center: [x, y, z0 + 1.5 - sink / 2], height: 3 + sink, startRadius: [b, b],
        endRadius: [d, d], segments: SEG}))
    if (P.boardFix === 'peg') {
      return union(post, cyl(x, y, top - 0.5, top + P.pcb + P.pegAbove, P.pegDiameter))
    }
    return subtract(post, cyl(x, y, top - P.standoffHoleDepth, top + 1, P.standoffHole))
  })
}

function bossCenters() {
  const c = inner / 2 - P.bossSize / 2
  return [[-c, -c], [c, -c], [-c, c], [c, c]]
}

// ------------------------------------------------------------------ rama
function frame() {
  const shell = subtract(
    box(-outer / 2, -outer / 2, 0, outer / 2, outer / 2, frontZ),
    box(-inner / 2, -inner / 2, -1, inner / 2, inner / 2, frontZ + 1))

  // kolnierz + skos 45 st. pod nim (druk bez podpor) jako jedna bryla
  const flangeInner = inner - 2 * P.flangeWidth
  const thin = 0.01
  const funnel = hulls.hull(
    box(-inner / 2, -inner / 2, gussetZ0 - 1, inner / 2, inner / 2, gussetZ0),
    box(-flangeInner / 2, -flangeInner / 2, flangeZ0, flangeInner / 2, flangeInner / 2, flangeZ0 + thin))
  const opening = box(-flangeInner / 2, -flangeInner / 2, flangeZ0 - 1,
    flangeInner / 2, flangeInner / 2, panelBackZ + 1)
  const flange = subtract(
    box(-inner / 2 - 0.05, -inner / 2 - 0.05, gussetZ0, inner / 2 + 0.05, inner / 2 + 0.05, panelBackZ),
    union(funnel, opening))

  const bosses = bossCenters().map(([x, y]) => subtract(
    box(x - P.bossSize / 2 - 0.01, y - P.bossSize / 2 - 0.01, 0,
      x + P.bossSize / 2 + 0.01, y + P.bossSize / 2 + 0.01, P.bossDepth),
    cyl(x, y, -1, P.bossDepth + 1, P.bossHole)))

  let body = union(shell, flange, ...bosses)

  // otwory na sruby do panelu + kieszenie od tylu
  const holes = insertPositions().flatMap(([x, y]) => [
    cyl(x, y, flangeZ0 - 1, panelBackZ + 1, P.screwHole),
    cyl(x, y, gussetZ0 - 1, flangeZ0 - 0.2, P.screwCounterbore),
  ])

  // wyjscie USB w dolnej sciance, otwarte od tylnej krawedzi; wyciecie idzie tez
  // przez skos pod kolnierzem - po obnizeniu obudowy wtyk USB jest na jego wysokosci
  const usb = usbCentersX().map((x) => box(x - P.usbWidth / 2, -outer / 2 - 1, -1,
    x + P.usbWidth / 2, -inner / 2 + P.flangeWidth + 0.5, P.usbDepth))

  // wentylacja w gornej sciance
  const vents = []
  for (let x = -30; x <= 30; x += 10) {
    vents.push(box(x - 1.2, inner / 2 - 0.5, 5, x + 1.2, outer / 2 + 1, 20))
  }

  // znacznik "gora": wciecie na tylnej krawedzi gornej scianki
  const mark = box(-3, inner / 2 - 0.5, -1, 3, outer / 2 + 1, 1.0)

  return subtract(body, ...holes, ...usb, ...vents, mark)
}

// ------------------------------------------------------------------ pokrywa
// Uklad pokrywy: z = 0 dawna strona zewnetrzna, z rosnie do srodka; coverExtra
// dokleja material od z = -coverExtra (ta strona lezy na stole).
function cover() {
  const t = P.coverThickness
  const e = P.coverExtra
  let plate = box(-outer / 2, -outer / 2, -e, outer / 2, outer / 2, t)

  // kolnierzyk wchodzacy w rame (z przerwami na narozniki i USB)
  const lo = inner - 2 * P.lipClearance
  const li = lo - 2 * P.lipWidth
  let lip = subtract(
    box(-lo / 2, -lo / 2, t, lo / 2, lo / 2, t + P.lipHeight),
    box(-li / 2, -li / 2, t - 1, li / 2, li / 2, t + P.lipHeight + 1))
  const cornerCut = P.bossSize + 1.5
  const cuts = []
  for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
    const x0 = sx > 0 ? lo / 2 - cornerCut : -lo / 2 - 1
    const y0 = sy > 0 ? lo / 2 - cornerCut : -lo / 2 - 1
    cuts.push(box(x0, y0, t - 1, x0 + cornerCut + 1, y0 + cornerCut + 1, t + P.lipHeight + 1))
  }
  for (const x of usbCentersX()) {
    cuts.push(box(x - P.usbWidth / 2 - 1, -lo / 2 - 1, t - 1,
      x + P.usbWidth / 2 + 1, -li / 2 + 1, t + P.lipHeight + 1))
  }
  lip = subtract(lip, ...cuts)

  // sterownik na slupkach
  const mounts = standoffs(t)

  // strzalka "gora" (wypukla, po wewnetrznej stronie)
  const arrow = extrudeLinear({height: 0.8},
    polygon({points: [[0, 36], [-6, 26], [-2, 26], [-2, 20], [2, 20], [2, 26], [6, 26]]}))
  const arrowPlaced = translate([0, 0, t - 0.01], arrow)

  let body = union(plate, lip, ...mounts, arrowPlaced)

  // otwory na sruby pokrywy (stozek pod leb wpuszczany M3, nad nim walec
  // o srednicy leba przez dodatkowa grubosc - leb siedzi o coverExtra glebiej)
  const screws = bossCenters().flatMap(([x, y]) => [
    cyl(x, y, -e - 1, t + 1, 3.4),
    cyl(x, y, -e - 1, 0, 6.6),
    cylinderElliptic({center: [x, y, 0.85], height: 1.7, startRadius: [3.3, 3.3],
      endRadius: [1.7, 1.7], segments: SEG}),
  ])

  // dziurki od klucza do zawieszenia (leb wchodzi w duzy otwor, pokrywa
  // zsuwa sie w dol, sruba zostaje w waskiej szczelinie)
  const keyholes = []
  for (const s of [-1, 1]) {
    const x = s * P.keyholeSpacing / 2
    const y = P.keyholeY
    keyholes.push(cyl(x, y - P.keyholeLength, -e - 1, t + 1, P.keyholeHead))
    keyholes.push(box(x - P.keyholeSlot / 2, y - P.keyholeLength, -e - 1, x + P.keyholeSlot / 2, y, t + 1))
    keyholes.push(cyl(x, y, -e - 1, t + 1, P.keyholeSlot))
  }

  // wentylacja u gory
  const vents = []
  for (let x = -28; x <= 28; x += 8) {
    vents.push(box(x - 1.25, 44, -e - 1, x + 1.25, 58, t + 1))
  }

  return subtract(body, ...screws, ...keyholes, ...vents)
}

// ------------------------------------------------------------------ test
// Plaski pierscien do sprawdzenia pasowania: kolnierz (3 mm) + 6 mm tulei.
// Drukuje sie kolnierzem na stole, bez podpor, ok. 40-60 min. Nakladasz go
// na tyl panelu: panel powinien wejsc w tuleje, a otwory trafic w gniazda.
function fitTest() {
  const top = panelBackZ + 6
  const ring = subtract(
    box(-outer / 2, -outer / 2, flangeZ0, outer / 2, outer / 2, top),
    box(-inner / 2, -inner / 2, panelBackZ, inner / 2, inner / 2, top + 1))
  const flangeInner = inner - 2 * P.flangeWidth
  const part = subtract(ring,
    box(-flangeInner / 2, -flangeInner / 2, flangeZ0 - 1, flangeInner / 2, flangeInner / 2, top + 1),
    ...insertPositions().map(([x, y]) => cyl(x, y, flangeZ0 - 1, top + 1, P.screwHole)))
  return translate([0, 0, -flangeZ0], part)
}

// Test sterownika: plytka 2 mm z wyzlobionym obrysem sterownika i 4 slupkami
// (ok. 20 min). Przykrec sterownik: sruby musza trafic w otwory, a nic na spodzie
// plytki nie moze zahaczac o slupki. Wciecie w krawedzi pokazuje, gdzie ma byc USB.
function boardTest() {
  const saved = P.boardOffsetX
  P.boardOffsetX = 0 // test zawsze wysrodkowany
  const base = 2.0
  const half = P.boardWidth / 2
  const yb = -inner / 2 + P.boardAboveBottom
  const usbX = P.usbOnRight ? half - P.usbFromBoardEdge : -half + P.usbFromBoardEdge
  // obrys i znacznik wycinane (odejmowanie daje czystsza siatke niz doklejanie)
  const plate = subtract(
    box(-half - 4, yb - 6, 0, half + 4, yb + P.boardHeight + 4, base),
    subtract(
      box(-half - 0.8, yb - 0.8, base - 0.6, half + 0.8, yb + P.boardHeight + 0.8, base + 1),
      box(-half, yb, base - 1, half, yb + P.boardHeight, base + 2)),
    box(usbX - 3, yb - 7, -1, usbX + 3, yb - 3, base + 1))
  const part = union(plate, ...standoffs(base, 1.0))
  P.boardOffsetX = saved
  return part
}

// ------------------------------------------------------------------ STL
function writeStl(geom, file) {
  // snap + wstawienie T-polaczen + triangulacja = szczelna siatka dla slicera
  const {generalize} = require('@jscad/modeling').modifiers
  const clean = generalize({snap: true, simplify: true, triangulate: true}, geom)
  const polys = geometries.geom3.toPolygons(clean)
  const tris = []
  for (const p of polys) {
    const v = p.vertices
    for (let i = 1; i < v.length - 1; i++) tris.push([v[0], v[i], v[i + 1]])
  }
  const buf = Buffer.alloc(84 + tris.length * 50)
  buf.write('obudowa panelu LED', 0)
  buf.writeUInt32LE(tris.length, 80)
  let o = 84
  for (const [a, b, c] of tris) {
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]]
    const w = [c[0] - a[0], c[1] - a[1], c[2] - a[2]]
    let n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
    const l = Math.hypot(...n) || 1
    n = n.map((q) => q / l)
    for (const q of [...n, ...a, ...b, ...c]) { buf.writeFloatLE(q, o); o += 4 }
    buf.writeUInt16LE(0, o); o += 2
  }
  fs.writeFileSync(file, buf)
  console.log(`${path.basename(file)}: ${tris.length} trojkatow`)
}

const out = process.argv[2] || '.'
console.log(`Sterownik: od pokrywy ${(P.boardAboveCover - P.boardSockets).toFixed(1)} mm luzu, ` +
  `od strony panelu na gniazdo z wtyczka tasmy zostaje ` +
  `${(P.extension - P.boardAboveCover - P.pcb).toFixed(1)} mm`)
writeStl(frame(), path.join(out, 'obudowa_rama.stl'))
writeStl(fitTest(), path.join(out, 'test_pasowania.stl'))
// pokrywa i test sterownika w dwoch wersjach mocowania plytki
for (const [fix, suffix] of [['screw', ''], ['peg', '_kolki']]) {
  P.boardFix = fix
  writeStl(cover(), path.join(out, `obudowa_pokrywa${suffix}.stl`))
  writeStl(boardTest(), path.join(out, `test_sterownik${suffix}.stl`))
}
console.log(`Rama: ${outer.toFixed(1)} x ${outer.toFixed(1)} x ${frontZ.toFixed(1)} mm`)
