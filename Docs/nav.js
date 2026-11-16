// Barra de navegación común a todas las páginas de Docs.
(function () {
  var paginas = [
    ["index.html", "Inicio"],
    ["01_tarea.html", "1. Qué pide la tarea"],
    ["02_sketch_cauchy.html", "2. Sketch de Cauchy"],
    ["03_cache.html", "3. Caché"],
    ["04_codigo.html", "4. Código y ejecución"],
    ["05_resultados.html", "5. Resultados"],
    ["06_informe.html", "6. Informe y preguntas"]
  ];
  var actual = location.pathname.split("/").pop() || "index.html";
  var nav = document.createElement("nav");
  nav.className = "top";
  paginas.forEach(function (p) {
    var a = document.createElement("a");
    a.href = p[0];
    a.textContent = p[1];
    if (p[0] === actual) a.className = "act";
    nav.appendChild(a);
  });
  document.body.insertBefore(nav, document.body.firstChild);
})();
