#let portada_uc3m(
  titulo: "",
  subtitulo: none,
  autores: (),
  curso: "2025/2026",
  asignatura: "",
  titulacion: "Grado en Ingeniería Informática",
  departamento: none,
  grupo: none,
  body
) = {
  // Configuración de página y numeración
  set page(
    paper: "a4",
    margin: (x: 2.5cm, y: 3cm),
    footer: context {
      if counter(page).get().first() > 1 {
        align(center, counter(page).display())
      }
    }
  )
  set text(font: "Latin Modern Roman 12", lang: "es", size: 11pt)
  
  // Portada centrada verticalmente
  align(center + horizon)[
    #image("Acrónimo_y_nombre_de_la_UC3M.svg")
    
    #if departamento != none [
      \ #text(1.8em)[#departamento]
    ]
    
    #v(1.5cm)
    #text(1.8em, weight: "semibold")[#asignatura] \
    #text(1.6em)[#titulacion]
    
    #v(2cm)
    #text(2.2em, weight: "bold")[#titulo]
    #if subtitulo != none [
      \ #v(0.5em) #text(1.6em)[#subtitulo]
    ]
    
    #v(2cm)
    #align(center)[
      #text(weight: "bold")[Miembros del grupo:] \
      #v(0.5em)
      #for autor in autores [
        #autor \
      ]
    ]
    
    #if grupo != none [
      #v(0.8cm)
      #strong(grupo)
    ]
    
    #v(1cm)
    #curso
  ]
  
  pagebreak()
  
  // Configuración del contenido post-portada
  counter(page).update(1)
  set par(justify: true)
  set heading(numbering: "1.1.")
  
  body
}