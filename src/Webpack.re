type webpackPlugin;

module NodeLoader = NodeLoader; /* Workaround bug in dune and melange: https://github.com/ocaml/dune/pull/6625 */
module Crypto = Crypto; /* Workaround bug in dune and melange: https://github.com/ocaml/dune/pull/6625 */

module HtmlWebpackPlugin = {
  [@bs.module "html-webpack-plugin"] [@bs.new]
  external make: Js.t('a) => webpackPlugin = "default";
};

module MiniCssExtractPlugin = {
  [@bs.module "mini-css-extract-plugin"] [@bs.new]
  external make: Js.t('a) => webpackPlugin = "default";

  [@bs.module "mini-css-extract-plugin"] [@bs.scope "default"]
  external loader: string = "loader";
};

module TerserPlugin = {
  type minifier;
  [@bs.module "terser-webpack-plugin"] [@bs.new]
  external make: Js.t('a) => webpackPlugin = "default";
  [@bs.module "terser-webpack-plugin"] [@bs.scope "default"]
  external swcMinify: minifier = "swcMinify";
  [@bs.module "terser-webpack-plugin"] [@bs.scope "default"]
  external esbuildMinify: minifier = "esbuildMinify";
};

module WebpackBundleAnalyzerPlugin = {
  // https://github.com/webpack-contrib/webpack-bundle-analyzer#options-for-plugin

  type analyzerMode = [ | `server | `static | `json | `disabled];

  type options = {
    analyzerMode,
    reportFilename: option(string),
    openAnalyzer: bool,
    analyzerPort: option(int),
  };

  [@bs.module "webpack-bundle-analyzer"] [@bs.new]
  external make': options => webpackPlugin = "BundleAnalyzerPlugin";

  module Mode = {
    // This module contains an interface that is exposed by rescript-ssg
    type staticModeOptions = {reportHtmlFilepath: string};

    type serverModeOptions = {port: int};

    type t =
      | Static(staticModeOptions)
      | Server(serverModeOptions);

    let makeOptions = (mode: t) => {
      switch (mode) {
      | Static({reportHtmlFilepath}) => {
          analyzerMode: `static,
          reportFilename: Some(reportHtmlFilepath),
          openAnalyzer: false,
          analyzerPort: None,
        }
      | Server({port}) => {
          analyzerMode: `server,
          reportFilename: None,
          openAnalyzer: false,
          analyzerPort: Some(port),
        }
      };
    };
  };

  let make = (mode: Mode.t) => mode->Mode.makeOptions->make';
};

[@bs.new] [@bs.module "webpack"] [@bs.scope "default"]
external definePlugin: Js.Dict.t(string) => webpackPlugin = "DefinePlugin";

[@bs.new] [@bs.module "webpack/lib/debug/ProfilingPlugin.js"]
external makeProfilingPlugin: unit => webpackPlugin = "default";

[@bs.new] [@bs.module "esbuild-loader"]
external makeESBuildPlugin: Js.t('a) => webpackPlugin = "EsbuildPlugin";

let getPluginWithGlobalValues =
    (globalEnvValuesDict: array((string, string))) => {
  Bundler.getGlobalEnvValuesDict(globalEnvValuesDict)->definePlugin;
};

module Webpack = {
  module Stats = {
    type t;

    type toStringOptions = {
      assets: bool,
      hash: bool,
      colors: bool,
    };

    [@bs.send] external hasErrors: t => bool = "hasErrors";
    [@bs.send] external hasWarnings: t => bool = "hasWarnings";
    [@bs.send] external toString': (t, toStringOptions) => string = "toString";
    [@bs.send] external toJson': (t, string) => Js.Json.t = "toJson";

    let toString = stats =>
      stats->toString'({assets: true, hash: true, colors: true});

    let toJson = stats => stats->toJson'("normal");
  };

  type compiler;

  [@bs.module "webpack"]
  external makeCompiler: Js.t({..}) => compiler = "default";

  [@bs.send]
  external run: (compiler, ('err, Js.Nullable.t(Stats.t)) => unit) => unit =
    "run";

  [@bs.send] external close: (compiler, 'closeError => unit) => unit = "close";
};

module WebpackDevServer = {
  type t;

  [@bs.new] [@bs.module "webpack-dev-server"]
  external make: (Js.t({..}), Webpack.compiler) => t = "default";

  [@bs.send]
  external startWithCallback: (t, unit => unit) => unit = "startCallback";

  [@bs.send] external stop: (t, unit) => Js.Promise.t(unit) = "stop";
};

module Mode = {
  type t =
    | Development
    | Production;

  let toString = (mode: t) =>
    switch (mode) {
    | Development => "development"
    | Production => "production"
    };
};

module Minimizer = {
  type t =
    | Terser
    | EsbuildPlugin
    | TerserPluginWithSwc
    | TerserPluginWithEsbuild;
};

module DevServerOptions = {
  module Proxy = {
    module DevServerTarget = {
      // https://github.com/DefinitelyTyped/DefinitelyTyped/blob/eefa7b7fce1443e2b6ee5e34d84e142880418208/types/http-proxy/index.d.ts#L25
      type params = {
        host: option(string),
        socketPath: option(string),
      };

      type t =
        | String(string)
        | Params(params);

      [@unboxed]
      type unboxed =
        | Any('a): unboxed;

      let makeUnboxed = (devServerTarget: t) =>
        switch (devServerTarget) {
        | String(s) => Any(s)
        | Params(devServerTarget) => Any(devServerTarget)
        };
    };

    type devServerPathRewrite = Js.Dict.t(string);

    type devServerProxyTo = {
      target: DevServerTarget.unboxed,
      pathRewrite: option(devServerPathRewrite),
      secure: bool,
      changeOrigin: bool,
      logLevel: string,
    };

    // https://webpack.js.org/configuration/dev-server/#devserverproxy
    type devServerProxy = Js.Dict.t(devServerProxyTo);

    type target =
      | Host(string)
      | UnixSocket(string);

    type pathRewrite = {
      from: string,
      to_: string,
    };

    type proxyTo = {
      target,
      pathRewrite: option(pathRewrite),
      secure: bool,
      changeOrigin: bool,
    };

    type t = {
      from: string,
      to_: proxyTo,
    };
  };

  type listenTo =
    | Port(int)
    | UnixSocket(string);

  type t = {
    listenTo,
    proxy: option(array(Proxy.t)),
  };
};

let dynamicPageSegmentPrefix = "dynamic__";

let makeConfig =
    (
      ~webpackBundleAnalyzerMode: option(WebpackBundleAnalyzerPlugin.Mode.t),
      ~devServerOptions: option(DevServerOptions.t),
      ~mode: Mode.t,
      ~minimizer: Minimizer.t,
      ~logger: Log.logger,
      ~outputDir: string,
      ~globalEnvValues: array((string, string)),
      ~renderedPages: array(RenderedPage.t),
    ) => {
  let entries =
    renderedPages
    ->Js.Array2.map(({path, entryPath, _}) =>
        (PageBuilderT.PagePath.toWebpackEntryName(path), entryPath)
      )
    ->Js.Dict.fromArray;

  let shouldMinimize = mode == Production;

  let config = {
    "entry": entries,

    "mode": Mode.toString(mode),

    "output": {
      "path": Bundler.getOutputDir(~outputDir),
      "publicPath": Bundler.assetPrefix,
      "filename": Bundler.assetsDirname ++ "/" ++ "js/[name]_[chunkhash].js",
      "assetModuleFilename":
        Bundler.assetsDirname ++ "/" ++ "[name].[hash][ext]",
      "hashFunction": Crypto.Hash.createMd5,
      "hashDigestLength": Crypto.Hash.digestLength,
      // Clean the output directory before emit.
      "clean": true,
    },

    "module": {
      "rules": [|
        {
          //
          "test": [%re {|/\.css$/|}],
          "use": [|MiniCssExtractPlugin.loader, "css-loader"|],
        },
        {"test": Bundler.assetRegex, "type": "asset/resource"}->Obj.magic,
      |],
    },

    "plugins": {
      let htmlWebpackPlugins =
        renderedPages->Js.Array2.map(({path, htmlTemplatePath, _}) => {
          HtmlWebpackPlugin.make({
            "template": htmlTemplatePath,
            "filename":
              Path.join2(PageBuilderT.PagePath.toString(path), "index.html"),
            "chunks": [|PageBuilderT.PagePath.toWebpackEntryName(path)|],
            "inject": true,
            "minify": {
              "collapseWhitespace": shouldMinimize,
              "keepClosingSlash": shouldMinimize,
              "removeComments": shouldMinimize,
              "removeRedundantAttributes": shouldMinimize,
              "removeScriptTypeAttributes": shouldMinimize,
              "removeStyleLinkTypeAttributes": shouldMinimize,
              "useShortDoctype": shouldMinimize,
              "minifyCSS": shouldMinimize,
            },
          })
        });

      let globalValuesPlugin = getPluginWithGlobalValues(globalEnvValues);

      let miniCssExtractPlugin =
        MiniCssExtractPlugin.make({
          "filename": Bundler.assetsDirname ++ "/" ++ "[name]_[chunkhash].css",
        });

      let webpackBundleAnalyzerPlugin =
        switch (webpackBundleAnalyzerMode) {
        | None => [||]
        | Some(mode) => [|WebpackBundleAnalyzerPlugin.make(mode)|]
        };

      Js.Array2.concat(
        htmlWebpackPlugins,
        [|miniCssExtractPlugin, globalValuesPlugin|],
      )
      ->Js.Array2.concat(webpackBundleAnalyzerPlugin);
    },
    // Explicitly disable source maps in dev mode
    "devtool": false,
    "optimization": {
      "runtimeChunk": {
        "name": "webpack-runtime",
      },
      "minimize": shouldMinimize,
      "minimizer": {
        switch (shouldMinimize, minimizer) {
        | (true, EsbuildPlugin) =>
          Some([|makeESBuildPlugin({"target": "es2015"})|])
        | (true, TerserPluginWithEsbuild) =>
          Some([|TerserPlugin.make({"minify": TerserPlugin.esbuildMinify})|])
        | (true, TerserPluginWithSwc) =>
          Some([|TerserPlugin.make({"minify": TerserPlugin.swcMinify})|])
        | (false, _)
        | (_, Terser) =>
          // Terser is used by default under the hood
          None
        };
      },
      "splitChunks": {
        "chunks": "all",
        "minSize": 20000,
        "cacheGroups": {
          "framework": {
            "priority": 40,
            "name": "framework",
            "test": {
              let frameworkPackages =
                [|"react", "react-dom", "scheduler", "prop-types"|]
                ->Js.Array2.joinWith("|");
              let regexStr = {j|(?<!node_modules.*)[\\\\/]node_modules[\\\\/]($(frameworkPackages))[\\\\/]|j};
              let regex = Js.Re.fromString(regexStr);
              regex;
            },
            "enforce": true,
          },
          "react-helmet": {
            "priority": 30,
            "name": "react-helmet",
            "test": {
              let packages = [|"react-helmet"|]->Js.Array2.joinWith("|");
              let regexStr = {j|[\\\\/]node_modules[\\\\/]($(packages))[\\\\/]|j};
              let regex = Js.Re.fromString(regexStr);
              regex;
            },
            "enforce": true,
          },
        },
      },
    },
    "watchOptions": {
      "aggregateTimeout": 1000,
    },
    "devServer": {
      switch (devServerOptions) {
      | None => None
      | Some({listenTo, proxy}) =>
        Some({
          // Prevent Webpack from handling SIGINT and SIGTERM signals
          // because we handle them in our graceful shutdown logic
          "setupExitSignals": false,
          "devMiddleware": {
            "stats": {
              switch (logger.logLevel) {
              | Info => "errors-warnings"
              | Debug => "normal"
              };
            },
          },
          "historyApiFallback": {
            "verbose": true,
            "rewrites": {
              // Here we add support for serving pages with dynamic path parts
              // We use underscore prefix in this library to mark this kind of paths: users/_id
              // Be build rewrite setting below like this:
              // from: "/^\/users\/.*/"
              // to: "/users/_id/index.html"
              let rewrites =
                renderedPages->Belt.Array.keepMap(page =>
                  switch (page.path) {
                  | Root => None
                  | Path(segments) =>
                    let hasDynamicPart =
                      segments
                      ->Js.Array2.find(segment =>
                          segment->Js.String2.startsWith(
                            dynamicPageSegmentPrefix,
                          )
                        )
                      ->Belt.Option.isSome;

                    switch (hasDynamicPart) {
                    | false => None
                    | _true =>
                      let pathWithAsterisks =
                        segments
                        ->Js.Array2.map(part =>
                            part->Js.String2.startsWith(
                              dynamicPageSegmentPrefix,
                            )
                              ? ".*" : part
                          )
                        ->Js.Array2.joinWith("/");

                      let regexString = "^/" ++ pathWithAsterisks;

                      let from = Js.Re.fromString(regexString);

                      let to_ =
                        Path.join3(
                          "/",
                          PageBuilderT.PagePath.toString(page.path),
                          "index.html",
                        );

                      Some({"from": from, "to": to_});
                    };
                  }
                );

              logger.info(() =>
                Js.log2("[Webpack dev server] Path rewrites: ", rewrites)
              );
              rewrites;
            },
          },
          "hot": false,
          // static: {
          //   directory: path.join(__dirname, "public"),
          // },
          "compress": true,
          "port": {
            switch (listenTo) {
            | Port(port) => Some(port)
            | UnixSocket(_) => None
            };
          },
          // TODO Should we check/remove socket file before starting or on terminating dev server?
          "ipc": {
            switch (listenTo) {
            | UnixSocket(path) => Some(path)
            | Port(_) => None
            };
          },
          "proxy": {
            switch (proxy) {
            | None => None
            | Some(proxySettings) =>
              let proxyDict:
                Js.Dict.t(DevServerOptions.Proxy.devServerProxyTo) =
                proxySettings
                ->Js.Array2.map(proxy => {
                    let proxyTo: DevServerOptions.Proxy.devServerProxyTo = {
                      target:
                        switch (proxy.to_.target) {
                        | Host(host) =>
                          DevServerOptions.Proxy.DevServerTarget.makeUnboxed(
                            String(host),
                          )
                        | UnixSocket(socketPath) =>
                          DevServerOptions.Proxy.DevServerTarget.makeUnboxed(
                            Params({
                              host: None,
                              socketPath: Some(socketPath),
                            }),
                          )
                        },
                      pathRewrite:
                        proxy.to_.pathRewrite
                        ->Belt.Option.map(({from, to_}) => {
                            Js.Dict.fromList([(from, to_)])
                          }),
                      secure: proxy.to_.secure,
                      changeOrigin: proxy.to_.changeOrigin,
                      logLevel: "debug",
                    };

                    (proxy.from, proxyTo);
                  })
                ->Js.Dict.fromArray;

              logger.debug(() =>
                Js.log2("[Webpack dev server] proxyDict: ", proxyDict)
              );

              Some(proxyDict);
            };
          },
        })
      };
    },
  };

  config;
};

let makeCompiler =
    (
      ~devServerOptions: option(DevServerOptions.t),
      ~logger: Log.logger,
      ~mode: Mode.t,
      ~minimizer: Minimizer.t,
      ~globalEnvValues: array((string, string)),
      ~outputDir,
      ~webpackBundleAnalyzerMode: option(WebpackBundleAnalyzerPlugin.Mode.t),
      ~renderedPages: array(RenderedPage.t),
    ) => {
  let config =
    makeConfig(
      ~devServerOptions,
      ~mode,
      ~logger,
      ~minimizer,
      ~outputDir,
      ~globalEnvValues,
      ~webpackBundleAnalyzerMode,
      ~renderedPages,
    );
  // TODO handle errors when we make compiler
  let compiler = Webpack.makeCompiler(config);
  (compiler, config);
};

let build =
    (
      ~mode: Mode.t,
      ~minimizer: Minimizer.t,
      ~logger: Log.logger,
      ~outputDir,
      ~globalEnvValues: array((string, string)),
      ~webpackBundleAnalyzerMode: option(WebpackBundleAnalyzerPlugin.Mode.t),
      ~renderedPages: array(RenderedPage.t),
    ) => {
  let durationLabel = "[Webpack.build] duration";
  Js.Console.timeStart(durationLabel);

  logger.info(() => Js.log("[Webpack.build] Building webpack bundle..."));

  let (compiler, _config) =
    makeCompiler(
      ~devServerOptions=None,
      ~mode,
      ~logger,
      ~outputDir,
      ~minimizer,
      ~globalEnvValues,
      ~webpackBundleAnalyzerMode: option(WebpackBundleAnalyzerPlugin.Mode.t),
      ~renderedPages,
    );

  compiler->Webpack.run((err, stats) => {
    switch (Js.Nullable.toOption(err)) {
    | Some(error) =>
      logger.info(() => {
        Js.Console.error2("[Webpack.build] Fatal error:", error);
        Process.exit(1);
      })
    | None =>
      logger.info(() => Js.log("[Webpack.build] Success!"));
      switch (Js.Nullable.toOption(stats)) {
      | None =>
        logger.info(() => {
          Js.Console.error("[Webpack.build] Error: stats object is None");
          Process.exit(1);
        })
      | Some(stats) =>
        logger.info(() => Js.log(Webpack.Stats.toString(stats)));

        switch (Webpack.Stats.hasErrors(stats)) {
        | false => ()
        | true =>
          Js.Console.error("[Webpack.build] Error: stats object has errors");
          Process.exit(1);
        };
        switch (Webpack.Stats.hasWarnings(stats)) {
        | false => ()
        | true =>
          logger.info(() => Js.log("[Webpack.build] Stats.hasWarnings"))
        };

        compiler->Webpack.close(closeError => {
          switch (Js.Nullable.toOption(closeError)) {
          | None => Js.Console.timeEnd(durationLabel)
          | Some(error) =>
            logger.info(() =>
              Js.log2("[Webpack.build] Compiler close error:", error)
            )
          }
        });
      };
    }
  });
};

let startDevServer =
    (
      ~devServerOptions: DevServerOptions.t,
      ~mode: Mode.t,
      ~minimizer: Minimizer.t,
      ~logger: Log.logger,
      ~outputDir,
      ~globalEnvValues: array((string, string)),
      ~webpackBundleAnalyzerMode: option(WebpackBundleAnalyzerPlugin.Mode.t),
      ~renderedPages: array(RenderedPage.t),
      ~onStart: unit => unit,
    ) => {
  logger.info(() => Js.log("[Webpack] Starting dev server..."));
  let startupDurationLabel = "[Webpack] WebpackDevServer startup duration";
  Js.Console.timeStart(startupDurationLabel);

  let (compiler, config) =
    makeCompiler(
      ~devServerOptions=Some(devServerOptions),
      ~mode,
      ~logger,
      ~outputDir,
      ~minimizer,
      ~globalEnvValues,
      ~webpackBundleAnalyzerMode,
      ~renderedPages,
    );

  let devServerOptions = config##devServer;

  switch (devServerOptions) {
  | None =>
    logger.info(() =>
      Js.Console.error(
        "[Webpack] Can't start dev server, config##devServer is None",
      )
    );
    Process.exit(1);
  | Some(devServerOptions) =>
    let devServer = WebpackDevServer.make(devServerOptions, compiler);
    devServer->WebpackDevServer.startWithCallback(() => {
      logger.info(() => {
        Js.log("[Webpack] WebpackDevServer started");
        Js.Console.timeEnd(startupDurationLabel);
        onStart();
      })
    });

    GracefulShutdown.addTask(() => {
      Js.log("[Webpack] Stopping dev server...");

      Js.Global.setTimeout(
        () => {
          Js.log("[Webpack] Failed to gracefully shutdown.");
          Process.exit(1);
        },
        GracefulShutdown.gracefulShutdownTimeout,
      )
      ->ignore;

      devServer
      ->WebpackDevServer.stop()
      ->Promise.map(() =>
          Js.log("[Webpack] Dev server stopped successfully")
        );
    });
  };
};
