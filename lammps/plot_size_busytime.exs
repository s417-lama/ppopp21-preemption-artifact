linewidth = 2

intvl = 1
# intvl = 2
# intvl = 3
thread = 55
sizes = [2, 4, 6, 8, 10, 12]
natoms = [256000, 2048000, 6912000, 16384000, 32000000, 55296000]

result_dir = "~/lammps/results/latest"

data = [
  {"omp_async"     , "Pthread"                , "#d62728", "dash"   , "x-thin"      },
  {"omp_async_nice", "Pthread (with nice)"    , "#9467bd", "dashdot", "diamond-open"},
  {"abt_sync"      , "Nonpreemptive Argobots" , "#1f77b4", "dot"    , "square-open" },
  {"abt_preemption", "Preemptive Argobots"    , "#2ca02c", "solid"  , "circle"      },
]

get_statistics_of_files = fn path ->
  data =
    path
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(fn path ->
      File.stream!(path)
      |> Stream.map(&String.split/1)
      |> Stream.map(fn
        ["[rank", _, "worker", _, "busy", "ratio:", ratio] -> Float.parse(ratio) |> elem(0)
        _                                                  -> nil
      end)
      |> Stream.filter(&!is_nil(&1))
      |> Stream.map(fn x -> 1 - x end)
      |> Enum.to_list()
      |> Statistics.mean()
    end)
  {Statistics.median(data), Statistics.stdev(data)}
end

data
|> Enum.map(fn {prefix, title, color, dash, symbol} ->
  {ys, errors} =
    Enum.map(sizes, fn size ->
      get_statistics_of_files.("#{result_dir}/#{prefix}_intvl_#{intvl}_thread_#{thread}_size_#{size}_*.out")
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
    title: %{text: "# of atoms"},
    showline: true,
    exponentformat: "power",
    range: [0, Enum.max(natoms) * 1.05],
  },
  yaxis: %{
    title: %{text: "Worker Idleness"},
    showline: true,
    range: [0, 0.6],
  },
  font: %{
    size: 21,
  },
  legend: %{
    x: 0.4,
    y: 1.0,
    bgcolor: "rgba(0,0,0,0)",
  },
  margin: %{
    l: 70,
    r: 0,
    b: 60,
    t: 10,
    pad: 0,
  },
})
|> PlotlyEx.show()
