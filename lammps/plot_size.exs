filename = System.argv() |> Enum.at(0)

linewidth = 2

intvl = 1
# intvl = 2
# intvl = 3

# thread = 55
sizes = [2, 4, 6, 8, 10, 12]
power = 7
natoms = [256000, 2048000, 6912000, 16384000, 32000000, 55296000] |> Enum.map(fn x -> x / :math.pow(10, power) end)

result_dir = "results/latest"

data = [
  {"omp_async"     , "Pthreads (w/o priority)", "#d62728", "dash"   , "x-thin"      },
  {"omp_async_nice", "Pthreads (w/ priority)" , "#9467bd", "dashdot", "diamond-open"},
  {"abt_sync"      , "Argobots (w/o priority)", "#1f77b4", "dot"    , "square-open" },
  {"abt_preemption", "Argobots (w/ priority)" , "#2ca02c", "solid"  , "circle"      },
]

get_statistics_of_files = fn(path, op_fn) ->
  data =
    path
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(fn path ->
      File.stream!(path)
      |> Stream.map(&String.split/1)
      |> Stream.map(fn
        ["Loop", "time", "of", time | _] -> Float.parse(time)
        _                                -> nil
      end)
      |> Stream.filter(&!is_nil(&1))
      |> Enum.to_list()
      |> case do
        [{time, ""}] -> time
        _            -> nil
      end
    end)
    |> Enum.map(op_fn)
  {Statistics.median(data), Statistics.stdev(data)}
end

baselines =
  Enum.map(sizes, fn size ->
    {baseline, _} = get_statistics_of_files.("#{result_dir}/abt_no_analysis_*_size_#{size}_*.out", &(&1))
    baseline
  end)

data
|> Enum.map(fn {prefix, title, color, dash, symbol} ->
  {ys, errors} =
    Enum.zip(sizes, baselines)
    |> Enum.map(fn {size, baseline} ->
      # get_statistics_of_files.("#{result_dir}/#{prefix}_intvl_#{intvl}_thread_#{thread}_size_#{size}_*.out", fn x -> x / baseline - 1 end)
      get_statistics_of_files.("#{result_dir}/#{prefix}_intvl_#{intvl}_size_#{size}_*.out", fn x -> x / baseline - 1 end)
    end)
    |> Enum.unzip()
  %{
    x: natoms,
    y: ys,
    error_y: %{
      type: "data",
      array: errors,
      visible: true,
    },
    type: "scatter",
    name: title,
    line: %{
      color: color,
      width: linewidth,
      dash: dash,
    },
    marker: %{
      symbol: symbol,
      size:   12,
      line:   %{width: linewidth, color: color},
    },
  }
end)
|> PlotlyEx.plot(%{
  width:  400,
  height: 350,
  xaxis: %{
    # title: %{text: "$\\text{# of atoms}\\; (\\times 10^#{power})$"},
    title: %{text: "# of atoms (&#x00D7; 10&#x207#{power};)"},
    showline: true,
    # exponentformat: "power",
    # dtick: 10_000_000,
    dtick: 1,
    range: [0, Enum.max(natoms) * 1.05],
  },
  yaxis: %{
    title: %{text: "Overhead"},
    tickformat: ",.0%",
    showline: true,
    range: [0, 2.6 / intvl],
  },
  font: %{
    size: 20,
  },
  legend: %{
    x: 0.4,
    y: 1.0,
    # bgcolor: "rgba(0,0,0,0)",
  },
  margin: %{
    l: 90,
    r: 10,
    b: 60,
    t: 10,
    pad: 0,
  },
})
|> PlotlyEx.show(filename: filename)
