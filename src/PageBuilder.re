module Fs = {
  [@module "fs"]
  external readFileSync: (string, string) => string = "readFileSync";

  [@module "fs"]
  external writeFileSync: (string, string) => unit = "writeFileSync";

  type makeDirSyncOptions = {recursive: bool};

  [@module "fs"]
  external mkDirSync: (string, makeDirSyncOptions) => unit = "mkdirSync";

  type rmSyncOptions = {
    force: bool,
    recursive: bool,
  };

  [@module "fs"] external rmSync: (string, rmSyncOptions) => unit = "rmSync";
};

module ChildProcess = {
  [@module "child_process"]
  external execSync: (. string, Js.t('a)) => int = "execSync";

  module Error = {
    [@get] external stdout: Js.Exn.t => string = "stdout";
  };
};

[@val] external import_: string => Js.Promise.t('a) = "import";

// Node caches imported modules, here is a workaround, but there is a possible memory leak:
// https://ar.al/2021/02/22/cache-busting-in-node.js-dynamic-esm-imports/
// Also: https://github.com/nodejs/modules/issues/307

let freshImport = modulePath => {
  let timestamp = Js.Date.now()->Js.Float.toString;
  let cacheBustingModulePath = {j|$(modulePath)?update=$(timestamp)|j};
  import_(cacheBustingModulePath);
};

let defaultRoot = {js|<div id="app"></div>|js};

let makeDefaultRootWithRenderedData = (data: string) => {j|<div id="app">$(data)</div>|j};

let makeHtmlTemplate = (helmet: ReactHelmet.helmetInstance, renderedHtml) => {
  let htmlAttributes = helmet.htmlAttributes.toString();
  let title = helmet.title.toString();
  let meta = helmet.meta.toString();
  let link = helmet.link.toString();
  let bodyAttributes = helmet.bodyAttributes.toString();
  {j|
<!doctype html>
<html $(htmlAttributes)>
  <head>
    <meta charset="utf-8"/>
    $(title)
    $(meta)
    $(link)
  </head>
  <body $(bodyAttributes)>
    <div id="app">$(renderedHtml)</div>
  </body>
</html>
|j};
};

let defaultReactRootName = "elementString";

let reactRootTemplate = {js|
switch (ReactDOM.querySelector("#app")) {
| Some(root) => ReactDOM.hydrate(elementString, root)
| None => ()
};
|js};

type prop('a) = {
  name: string,
  value: 'a,
};

type component('a) =
  | ComponentWithoutProps(React.element)
  | ComponentWithOneProp(oneProp('a))
and oneProp('a) = {
  component: 'a => React.element,
  prop: prop('a),
};

type page('a) = {
  component: component('a),
  moduleName: string,
  modulePath: string,
  path: string,
};

let indexHtmlFilename = "index.html";

let makeReactAppModuleName = (~pagePath, ~moduleName) => {
  let modulePrefix =
    pagePath
    ->Js.String2.replace("/", "")
    ->Js.String2.replace("-", "")
    ->Js.String2.replace(".", "");

  modulePrefix ++ moduleName ++ "App";
};

let buildPageHtmlAndReactApp = (~outputDir, page: page('a)) => {
  let {component, moduleName, path: pagePath, _} = page;

  let pageOutputDir = Path.join2(outputDir, pagePath);

  Js.log2(
    "[PageBuilder.buildPageHtmlAndReactApp] Output dir for page: ",
    pageOutputDir,
  );

  let resultHtmlPath = Path.join2(pageOutputDir, indexHtmlFilename);

  let () = {
    Fs.mkDirSync(pageOutputDir, {recursive: true});
  };

  let (element, elementString) = {
    switch (component) {
    | ComponentWithoutProps(element) => (element, "<" ++ moduleName ++ " />")
    | ComponentWithOneProp({component, prop}) =>
      // We need to inject prop value to react app template. This is pretty unsafe part, but should work.
      let unsafeStringifiedPropValue =
        switch (prop.value->Js.Json.stringifyAny) {
        | Some(propValueString) => {j|{{js|$(propValueString)|js}->Js.Json.parseExn->Obj.magic}|j}
        | None =>
          // Js.Json.stringifyAny(None) returns None. No need to do anything with it, can be injected to template as is.
          "None"
        };

      let element = component(prop.value);
      let elementString =
        "<"
        ++ moduleName
        ++ " "
        ++ prop.name
        ++ "="
        ++ unsafeStringifiedPropValue
        ++ " />";

      (element, elementString);
    };
  };

  let html = ReactDOMServer.renderToString(element);

  let htmlWithStyles = Emotion.Server.renderStylesToString(html);

  let helmet = ReactHelmet.renderStatic();

  let resultHtml = makeHtmlTemplate(helmet, htmlWithStyles);

  let resultReactApp =
    reactRootTemplate->Js.String2.replace(
      defaultReactRootName,
      elementString,
    );

  let pageAppModuleName = makeReactAppModuleName(~pagePath, ~moduleName);

  let () = {
    let resultReactRescriptAppFilename = pageAppModuleName ++ ".re";
    Fs.writeFileSync(resultHtmlPath, resultHtml);
    Fs.writeFileSync(
      Path.join2(pageOutputDir, resultReactRescriptAppFilename),
      resultReactApp,
    );
  };

  let () = {
    let resultReactCompiledAppFilename = pageAppModuleName ++ ".bs.js";
    let webpackPage: Webpack.page = {
      title: pageAppModuleName,
      path: pagePath,
      entryPath: Path.join2(pageOutputDir, resultReactCompiledAppFilename),
      outputDir: pageOutputDir,
      htmlTemplatePath: resultHtmlPath,
    };
    Webpack.pages->Js.Dict.set(pageAppModuleName, webpackPage);
  };

  Js.log2(
    "[PageBuilder.buildPageHtmlAndReactApp] Build finished: ",
    moduleName,
  );
};

let makeUniqueArray = array => Set.fromArray(array)->Set.toArray;

let rebuildPagesWithWorker = (~outputDir, pages: array(page('a))) => {
  let rebuildPages =
    pages->Js.Array2.map(page => {
      let rebuildPage: RebuildPageWorkerT.rebuildPage = {
        modulePath: page.modulePath,
        outputDir,
        path: page.path,
      };

      rebuildPage;
    });

  WorkingThreads.runRebuildPageWorker(~workerData=rebuildPages);
};

let getModuleDependencies = (~modulePath) =>
  DependencyTree.makeList({
    filename: modulePath,
    // TODO Fix me. Is it really needed? Should it be func argument?
    directory: "/Users/denis/projects/builder/.vscode",
    filter: path => path->Js.String2.indexOf("node_modules") == (-1),
  });

// To make watcher work properly we need to:
// Monitor changes in a module itself and monitor changes in all dependencies of a module (except node modules?)
// After a module changed should we refresh dependencies and remove stale?

let startWatcher = (~outputDir, pages: list(page('a))) => {
  let modulePathToPagesDict = Js.Dict.empty();
  pages->Belt.List.forEach(page => {
    switch (modulePathToPagesDict->Js.Dict.get(page.modulePath)) {
    | None => modulePathToPagesDict->Js.Dict.set(page.modulePath, [|page|])
    | Some(pages) =>
      modulePathToPagesDict->Js.Dict.set(
        page.modulePath,
        Js.Array2.concat([|page|], pages),
      )
    }
  });

  let pageModulePaths =
    modulePathToPagesDict
    ->Js.Dict.entries
    ->Belt.Array.keepMap(((_, pages)) =>
        pages->Belt.Array.get(0)->Belt.Option.map(page => page.modulePath)
      );

  let modulesAndDependencies =
    pageModulePaths->Js.Array2.map(modulePath => {
      let dependencies = getModuleDependencies(~modulePath);

      (modulePath, dependencies);
    });

  let dependencyToPageModuleDict = Js.Dict.empty();

  let updateDependencyToPageModuleDict = (~dependency, ~modulePath) => {
    switch (dependencyToPageModuleDict->Js.Dict.get(dependency)) {
    | None =>
      dependencyToPageModuleDict->Js.Dict.set(dependency, [|modulePath|])
    | Some(pageModules) =>
      dependencyToPageModuleDict->Js.Dict.set(
        dependency,
        Js.Array2.concat([|modulePath|], pageModules)->makeUniqueArray,
      )
    };
  };

  modulesAndDependencies->Js.Array2.forEach(((modulePath, dependencies)) => {
    dependencies->Js.Array2.forEach(dependency =>
      updateDependencyToPageModuleDict(~dependency, ~modulePath)
    )
  });

  let allDependencies = {
    let dependencies =
      dependencyToPageModuleDict
      ->Js.Dict.entries
      ->Js.Array2.map(((dependency, _pageModules)) => dependency);

    Js.Array2.concat(pageModulePaths, dependencies);
  };

  Js.log2("[Watcher] Initial watcher dependencies: ", allDependencies);

  let watcher = Chokidar.chokidar->Chokidar.watchFiles(allDependencies);

  let rebuildQueueRef: ref(array(string)) = ref([||]);

  watcher->Chokidar.onChange(filepath => {
    Js.log2("[Watcher] File changed: ", filepath);

    switch (modulePathToPagesDict->Js.Dict.get(filepath)) {
    | Some(_) =>
      Js.log2("[Watcher] Exact page module changed:", filepath);
      let newQueue =
        Js.Array2.concat([|filepath|], rebuildQueueRef^)->makeUniqueArray;
      rebuildQueueRef := newQueue;
    | None =>
      switch (dependencyToPageModuleDict->Js.Dict.get(filepath)) {
      | None =>
        // Nothing depends on changed file. Should we remove it from watcher?
        Js.log2("[Watcher] No pages depend on file:", filepath)
      | Some(pageModules) =>
        // Page dependency changed. Should rebuild pages that depend on it.
        Js.log2("[Watcher] Should rebuild these pages:", pageModules);
        let newQueue =
          Js.Array2.concat(pageModules, rebuildQueueRef^)->makeUniqueArray;
        rebuildQueueRef := newQueue;
      }
    };
  });

  let _intervalId =
    Js.Global.setInterval(
      () => {
        switch (rebuildQueueRef^) {
        | [||] => ()
        | rebuildQueue =>
          Js.log2("[Watcher] Page modules to rebuild queue: ", rebuildQueue);

          let pagesToRebuild =
            rebuildQueue
            ->Js.Array2.map(modulePath => {
                switch (modulePathToPagesDict->Js.Dict.get(modulePath)) {
                | None =>
                  Js.log2(
                    "[Watcher] Can't rebuild page, page module is missing in dict: ",
                    modulePath,
                  );
                  None;
                | Some(pages) => Some(pages)
                }
              })
            ->Belt.Array.keepMap(v => v)
            ->Belt.Array.concatMany;

          rebuildPagesWithWorker(~outputDir, pagesToRebuild)
          ->Promise.map(_ => {
              Js.log("[Watcher] Updating dependencies to watch");

              pagesToRebuild->Js.Array2.forEach(page => {
                let modulePath = page.modulePath;
                let newDependencies = getModuleDependencies(~modulePath);

                Js.log3(
                  "[Watcher] New dependencies of the module ",
                  modulePath,
                  newDependencies,
                );

                newDependencies->Js.Array2.forEach(dependency =>
                  updateDependencyToPageModuleDict(~dependency, ~modulePath)
                );

                watcher->Chokidar.add(newDependencies);

                Js.log2(
                  "[Watcher] !!! dependencyToPageModuleDict",
                  dependencyToPageModuleDict,
                );
              });
            })
          ->ignore;

          rebuildQueueRef := [||];
        }
      },
      1500,
    );

  ();
};

let buildPages = (~outputDir, pages: list(page('a))) => {
  Js.log("[PageBuilder.buildPages] Building pages...");

  let pagesDict = Js.Dict.empty();

  let () =
    pages->Belt.List.forEach(page => {
      switch (pagesDict->Js.Dict.get(page.path)) {
      | None => pagesDict->Js.Dict.set(page.path, page)
      | Some(_) =>
        Js.log3(
          "[PageBuilder.buildPages] List of pages contains pages with the same paths. Page with path: ",
          page.path,
          " has already been built.",
        );

        Process.exit(1);
      };

      buildPageHtmlAndReactApp(~outputDir, page);
    });

  pagesDict;
};

let start = (~pages: list(page('a)), ~outputDir, ~webpackOutputDir, ~mode) => {
  let _pagesDict = buildPages(~outputDir, pages);

  startWatcher(~outputDir, pages);

  Webpack.startDevServer(~mode, ~webpackOutputDir);
};

let build =
    (~pages, ~outputDir, ~webpackOutputDir, ~rescriptBinaryPath, ~mode) => {
  let _pagesDict = buildPages(~outputDir, pages);

  Js.log("[PageBuilder.build] Compiling React app files...");

  switch (ChildProcess.execSync(. rescriptBinaryPath, {"encoding": "utf8"})) {
  | exception (Js.Exn.Error(error)) =>
    Js.log2(
      "[PageBuilder.build] Rescript build failed:\n",
      error->Js.Exn.message,
    );
    Js.log2(
      "[PageBuilder.build] Rescript build failed:\n",
      error->ChildProcess.Error.stdout,
    );
    Process.exit(1);
  | stdout => Js.log2("[PageBuilder.build] Rescript build success:\n", stdout)
  };

  Js.log("[PageBuilder.build] Building webpack bundle...");

  Webpack.build(~mode, ~webpackOutputDir, ~verbose=true);
};
