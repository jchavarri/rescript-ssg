let checkDuplicatedPagePaths = (pages: array(array(PageBuilder.page))) => {
  Js.log("[rescript-ssg] Checking duplicated page paths...");

  let pagesDict = Js.Dict.empty();

  pages->Js.Array2.forEach(pages' => {
    pages'->Js.Array2.forEach(page => {
      let pagePath = PageBuilderT.PagePath.toString(page.path);
      switch (pagesDict->Js.Dict.get(pagePath)) {
      | None => pagesDict->Js.Dict.set(pagePath, page)
      | Some(_) =>
        Js.Console.error2(
          "[rescript-ssg] List of pages contains pages with the same paths. Duplicated page path:",
          pagePath,
        );
        Process.exit(1);
      };
    })
  });
};

let compileRescript = (~compileCommand: string, ~logger: Log.logger) => {
  let durationLabel = "[Commands.compileRescript] Success! Duration";
  Js.Console.timeStart(durationLabel);

  logger.info(() =>
    Js.log("[Commands.compileRescript] Compiling fresh React app files...")
  );

  switch (
    ChildProcess.spawnSync(
      compileCommand,
      [||],
      {"shell": true, "encoding": "utf8", "stdio": "inherit"},
    )
  ) {
  | Ok () => logger.info(() => Js.Console.timeEnd(durationLabel))
  | Error(JsError(error)) =>
    logger.info(() => {
      Js.Console.error2("[Commands.compileRescript] Failure! Error:", error)
    });
    Process.exit(1);
  | Error(ExitCodeIsNotZero(exitCode)) =>
    Js.Console.error2(
      "[Commands.compileRescript] Failure! Exit code is not zero:",
      exitCode,
    );
    Process.exit(1);
  | exception (Js.Exn.Error(error)) =>
    logger.info(() => {
      Js.Console.error2(
        "[Commands.compileRescript] Exception:\n",
        error->Js.Exn.message,
      )
    });
    Process.exit(1);
  };
};

type generatedFilesSuffix =
  | NoSuffix
  | UnixTimestamp;

let initializeAndBuildPages =
    (
      ~logLevel,
      ~buildWorkersCount,
      ~pages: array(array(PageBuilder.page)),
      ~outputDir,
      ~melangeOutputDir,
      ~globalEnvValues,
      ~generatedFilesSuffix,
      ~bundlerMode: Bundler.mode,
    ) => {
  let () = checkDuplicatedPagePaths(pages);

  let logger = Log.makeLogger(logLevel);

  let pages =
    switch (Bundler.bundler, bundlerMode) {
    | (Esbuild, Watch) =>
      pages->Js.Array2.map(pages =>
        pages->Js.Array2.map(page =>
          {
            ...page,
            // Add a script to implement live reloading with esbuild
            // https://esbuild.github.io/api/#live-reload
            headScripts:
              Js.Array2.concat(
                [|Esbuild.subscribeToRebuildEventScript|],
                page.headScripts,
              ),
          }
        )
      )
    | _ => pages
    };

  let renderedPages =
    BuildPageWorkerHelpers.buildPagesWithWorkers(
      ~buildWorkersCount,
      ~pages,
      ~outputDir,
      ~melangeOutputDir,
      ~logger,
      ~globalEnvValues,
      ~exitOnPageBuildError=true,
      ~generatedFilesSuffix=
        switch (generatedFilesSuffix) {
        | NoSuffix => ""
        | UnixTimestamp =>
          "_" ++ Js.Date.make()->Js.Date.valueOf->Belt.Float.toString
        },
    );

  (logger, pages, renderedPages);
};

let build =
    (
      ~outputDir: string,
      ~projectRootDir: string,
      ~melangeOutputDir: option(string)=?,
      ~compileCommand: string,
      ~logLevel: Log.level,
      ~mode: Webpack.Mode.t,
      ~pages: array(array(PageBuilder.page)),
      ~webpackBundleAnalyzerMode:
         option(Webpack.WebpackBundleAnalyzerPlugin.Mode.t)=None,
      ~minimizer: Webpack.Minimizer.t=Terser,
      ~globalEnvValues: array((string, string))=[||],
      ~generatedFilesSuffix: generatedFilesSuffix=UnixTimestamp,
      ~buildWorkersCount: option(int)=?,
      (),
    ) => {
  let (logger, _pages, renderedPages) =
    initializeAndBuildPages(
      ~logLevel,
      ~buildWorkersCount,
      ~pages,
      ~outputDir,
      ~melangeOutputDir,
      ~globalEnvValues,
      ~generatedFilesSuffix,
      ~bundlerMode=Build,
    );

  renderedPages
  ->Promise.map(renderedPages => {
      let () = compileRescript(~compileCommand, ~logger);

      switch (Bundler.bundler) {
      | Esbuild =>
        let () =
          Esbuild.build(
            ~outputDir,
            ~projectRootDir,
            ~globalEnvValues,
            ~renderedPages,
          )
          ->ignore;
        ();
      | Webpack =>
        let () =
          Webpack.build(
            ~mode,
            ~outputDir,
            ~logger,
            ~webpackBundleAnalyzerMode,
            ~minimizer,
            ~globalEnvValues,
            ~renderedPages,
          );
        ();
      };
    })
  ->ignore;
};

let start =
    (
      ~outputDir: string,
      ~projectRootDir: string,
      ~melangeOutputDir: option(string)=?,
      ~mode: Webpack.Mode.t,
      ~logLevel: Log.level,
      ~pages: array(array(PageBuilder.page)),
      ~devServerOptions: Webpack.DevServerOptions.t,
      ~webpackBundleAnalyzerMode:
         option(Webpack.WebpackBundleAnalyzerPlugin.Mode.t),
      ~minimizer: Webpack.Minimizer.t=Terser,
      ~globalEnvValues: array((string, string))=[||],
      ~generatedFilesSuffix: generatedFilesSuffix=UnixTimestamp,
      ~buildWorkersCount: option(int)=?,
      ~esbuildMainServerPort: int=8000,
      ~esbuildProxyServerPort: int=8001,
      (),
    ) => {
  let (logger, pages, renderedPages) =
    initializeAndBuildPages(
      ~logLevel,
      ~buildWorkersCount,
      ~pages,
      ~outputDir,
      ~melangeOutputDir,
      ~globalEnvValues,
      ~generatedFilesSuffix,
      ~bundlerMode=Watch,
    );

  let startFileWatcher = () =>
    Watcher.startWatcher(
      ~outputDir,
      ~melangeOutputDir,
      ~logger,
      ~globalEnvValues,
      pages,
    );

  // rescript-ssg just emitted reason artifacts and JS compilation is happening...
  // Starting dev server after a little delay.
  // Ideally, we want to start dev server and file watcher after JS compilation is done
  // to avoid redundant rebuilds while JS is still compiling.
  let delayBeforeDevServerStart = 3000;

  Js.Global.setTimeout(
    () => {
      renderedPages
      ->Promise.map(renderedPages =>
          switch (Bundler.bundler) {
          | Esbuild =>
            Esbuild.watchAndServe(
              ~outputDir,
              ~projectRootDir,
              ~globalEnvValues,
              ~renderedPages,
              ~port=esbuildMainServerPort,
            )
            ->Promise.map(serveResult => {
                let () =
                  ProxyServer.start(
                    ~port=esbuildProxyServerPort,
                    ~targetHost=serveResult.host,
                    ~targetPort=serveResult.port,
                  );
                let () = startFileWatcher();
                ();
              })
            ->ignore
          | Webpack =>
            let () =
              Webpack.startDevServer(
                ~devServerOptions,
                ~webpackBundleAnalyzerMode,
                ~mode,
                ~logger,
                ~outputDir,
                ~minimizer,
                ~globalEnvValues,
                ~renderedPages,
                ~onStart=startFileWatcher,
              );
            ();
          }
        )
      ->ignore
    },
    delayBeforeDevServerStart,
  )
  ->ignore;
};
