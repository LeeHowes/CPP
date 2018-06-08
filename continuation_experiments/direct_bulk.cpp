#include <functional>
#include <future>
#include <optional>
#include <iostream>

template<class F, class Shape, class RF>
auto bulk_then_value(
    F&& continuationFunction,
    Shape s,
    RF&& resultFactory) {
  return [continuationFunction = std::forward<F>(continuationFunction),
          resultFactory = std::forward<RF>(resultFactory),
          s = std::forward<Shape>(s)](
        auto&& outputPromise) mutable {
    using OutputPromiseRef = decltype(outputPromise);
    using OutputPromise = typename std::remove_reference<OutputPromiseRef>::type;

    class InputPromise {
    public:
        InputPromise(
            F&& f, OutputPromise&& outputPromise, int initialResult, int shape) :
            f_(std::move(f)), outputPromise_(std::move(outputPromise)), result_{initialResult}, shape_{shape} {}

        void set_value(int value) {
          inputValue_ = std::move(value);
        }

        void set_exception(std::exception_ptr e) {
          outputException_ = std::move(e);
        }

        const int& get_shape() const {
          return shape_;
        }

        void execute_at(int idx) { // Should be templated Shape
            if(inputValue_) {
                f_(*inputValue_, idx, result_);
            }
        }

        void done() {
          if(outputException_) {
            outputPromise_.set_exception(*std::move(outputException_));
          } else {
            outputPromise_.set_value(std::move(result_));
          }
        }

    private:
        F f_;
        OutputPromise outputPromise_;
        std::optional<int> inputValue_;
        std::optional<std::exception_ptr> outputException_;
        int result_;
        int shape_;
    };

    return InputPromise(
        std::move(continuationFunction),
        std::forward<OutputPromiseRef>(outputPromise),
        resultFactory(),
        s);
  };

}

template<class Continuation>
int then_execute(Continuation&& cont, int inputFuture) {
  class Promise {
  public:
      Promise(int& resultStorage, std::optional<std::exception_ptr>& exceptionStorage) : resultStorage_(resultStorage), exceptionStorage_(exceptionStorage) {}

      void set_value(int value) {
          resultStorage_ = value;
      }

      void set_exception(std::exception_ptr e) {
          exceptionStorage_ = std::move(e);
      }

  private:
      int& resultStorage_;
      std::optional<std::exception_ptr>& exceptionStorage_;
  };


  int resultStorage;
  std::optional<std::exception_ptr> exceptionStorage;
  auto boundCont = std::forward<Continuation>(cont)(
    Promise{resultStorage, exceptionStorage});

  const auto& shape = boundCont.get_shape();
  boundCont.set_value(inputFuture);
  for(int i = 0; i < shape; ++i) {
    boundCont.execute_at(i);
  }
  boundCont.done();
  return resultStorage;
}

int main() {
  auto inputFuture = 2;
  auto p = bulk_then_value(
      [](const int& a, int /*idx*/, int &out){out+=a;},
      int{20}, // Shape
      []() -> int {return 0;});
  auto resultF = then_execute(p, inputFuture);
  std::cout << resultF << "\n";

  return resultF;
}
